#include "trc2026_manual/manual_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace trc2026_manual
{

ManualController::ManualController(const rclcpp::NodeOptions & options)
: BaseManualController("manual_controller_node", options)
{
  cmd_vel_publisher_ = this->create_publisher<geometry_msgs::msg::Twist>("cmd_vel", 1);
  arm_velocity_publisher_ = this->create_publisher<std_msgs::msg::Float64MultiArray>(
    "/arm_velocity_controller/commands", 1);
  arm_position_publisher_ = this->create_publisher<trajectory_msgs::msg::JointTrajectory>(
    "/arm_position_controller/position_targets", 1);
  recovery_subscription_ = this->create_subscription<std_msgs::msg::Bool>(
    "/arm_position_controller/recovery_ready", rclcpp::QoS(1).transient_local(),
    [this](std_msgs::msg::Bool::ConstSharedPtr msg) {
      if (!msg->data || !recovery_ready_) {
        resync_position_targets_.fill(true);
        arm_position_targets_initialized_ = false;
        arm_4_axis_command_ = 0.0;
        arm_123_axis_command_ = 0.0;
        selected_arm_123_joint_ = -1;
      }
      recovery_ready_ = msg->data;
    });
  joint_state_subscription_ = this->create_subscription<sensor_msgs::msg::JointState>(
    "/joint_states", 10,
    std::bind(&ManualController::joint_state_callback, this, std::placeholders::_1));
  health_subscription_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
    "/robstride/health", 10,
    std::bind(&ManualController::health_callback, this, std::placeholders::_1));

  this->declare_parameter("axis_linear_x", 4);
  this->declare_parameter("axis_linear_y", 3);
  this->declare_parameter("axis_angular_z", 0);
  this->declare_parameter("scale_linear_x", 1.0);
  this->declare_parameter("scale_linear_y", 1.0);
  this->declare_parameter("scale_angular_z", 1.0);
  this->declare_parameter("arm_y_axis", 6);
  this->declare_parameter("arm_4_positive_axis", 2);
  this->declare_parameter("arm_4_negative_axis", 5);
  this->declare_parameter("arm_123_axis", 7);
  this->declare_parameter("arm_1_button", 1);
  this->declare_parameter("arm_2_button", 2);
  this->declare_parameter("arm_3_button", 3);
  this->declare_parameter("gripper_positive_button", 4);
  this->declare_parameter("gripper_negative_button", 5);
  this->declare_parameter("arm_y_velocity_scale", 20000.0);
  this->declare_parameter("arm_4_position_rate", 10.0);
  this->declare_parameter("arm_123_position_rate", 0.5);
  this->declare_parameter("arm_position_trajectory_duration", 0.1);
  this->declare_parameter("gripper_velocity_scale", 5000.0);
  this->declare_parameter("deadzone", 0.01);
  this->declare_parameter("joy_timeout", 0.5);

  this->get_parameter("axis_linear_x", axis_linear_x_);
  this->get_parameter("axis_linear_y", axis_linear_y_);
  this->get_parameter("axis_angular_z", axis_angular_z_);
  this->get_parameter("scale_linear_x", scale_linear_x_);
  this->get_parameter("scale_linear_y", scale_linear_y_);
  this->get_parameter("scale_angular_z", scale_angular_z_);
  this->get_parameter("arm_y_axis", arm_y_axis_);
  this->get_parameter("arm_4_positive_axis", arm_4_positive_axis_);
  this->get_parameter("arm_4_negative_axis", arm_4_negative_axis_);
  this->get_parameter("arm_123_axis", arm_123_axis_);
  this->get_parameter("arm_1_button", arm_1_button_);
  this->get_parameter("arm_2_button", arm_2_button_);
  this->get_parameter("arm_3_button", arm_3_button_);
  this->get_parameter("gripper_positive_button", gripper_positive_button_);
  this->get_parameter("gripper_negative_button", gripper_negative_button_);
  this->get_parameter("arm_y_velocity_scale", arm_y_velocity_scale_);
  this->get_parameter("arm_4_position_rate", arm_4_position_rate_);
  this->get_parameter("arm_123_position_rate", arm_123_position_rate_);
  this->get_parameter("arm_position_trajectory_duration", arm_position_trajectory_duration_);
  if (!std::isfinite(arm_position_trajectory_duration_) || arm_position_trajectory_duration_ <= 0.0) {
    throw std::invalid_argument("arm_position_trajectory_duration must be finite and positive");
  }
  this->get_parameter("gripper_velocity_scale", gripper_velocity_scale_);
  this->get_parameter("deadzone", deadzone_);
  this->get_parameter("joy_timeout", joy_timeout_sec_);

  last_joy_time_ = this->now();
  publish_timer_ = this->create_wall_timer(
    std::chrono::milliseconds(50), std::bind(&ManualController::publish_timer_callback, this));
}

void ManualController::joy_callback(const sensor_msgs::msg::Joy::SharedPtr msg)
{
  last_joy_time_ = this->now();
  const auto axis_value = [&msg](int index) {
    return index >= 0 && static_cast<size_t>(index) < msg->axes.size() ? msg->axes[index] : 0.0;
  };
  const auto deadzoned = [this](double value) {
    return std::abs(value) > deadzone_ ? value : 0.0;
  };

  last_cmd_vel_.linear.x = axis_value(axis_linear_x_) * scale_linear_x_;
  last_cmd_vel_.linear.y = axis_value(axis_linear_y_) * scale_linear_y_;
  last_cmd_vel_.angular.z = axis_value(axis_angular_z_) * scale_angular_z_;
  arm_y_velocity_command_ = deadzoned(axis_value(arm_y_axis_)) * arm_y_velocity_scale_;
  const auto positive_trigger =
    std::clamp((1.0 - axis_value(arm_4_positive_axis_)) / 2.0, 0.0, 1.0);
  const auto negative_trigger =
    std::clamp((1.0 - axis_value(arm_4_negative_axis_)) / 2.0, 0.0, 1.0);
  arm_4_axis_command_ = deadzoned(positive_trigger - negative_trigger);

  const bool arm_1_selected = arm_1_button_ >= 0 &&
    static_cast<size_t>(arm_1_button_) < msg->buttons.size() && msg->buttons[arm_1_button_];
  const bool arm_2_selected = arm_2_button_ >= 0 &&
    static_cast<size_t>(arm_2_button_) < msg->buttons.size() && msg->buttons[arm_2_button_];
  const bool arm_3_selected = arm_3_button_ >= 0 &&
    static_cast<size_t>(arm_3_button_) < msg->buttons.size() && msg->buttons[arm_3_button_];
  const int selected_count = static_cast<int>(arm_1_selected) +
    static_cast<int>(arm_2_selected) + static_cast<int>(arm_3_selected);
  selected_arm_123_joint_ = selected_count == 1 ?
    (arm_1_selected ? 0 : (arm_2_selected ? 1 : 2)) : -1;
  arm_123_axis_command_ = selected_arm_123_joint_ >= 0 ?
    deadzoned(axis_value(arm_123_axis_)) : 0.0;

  const bool positive_button = gripper_positive_button_ >= 0 &&
    static_cast<size_t>(gripper_positive_button_) < msg->buttons.size() &&
    msg->buttons[gripper_positive_button_];
  const bool negative_button = gripper_negative_button_ >= 0 &&
    static_cast<size_t>(gripper_negative_button_) < msg->buttons.size() &&
    msg->buttons[gripper_negative_button_];
  gripper_velocity_command_ = positive_button == negative_button ? 0.0 :
    (positive_button ? gripper_velocity_scale_ : -gripper_velocity_scale_);
}

void ManualController::publish_timer_callback()
{
  constexpr double period_sec = 0.05;
  const bool command_is_fresh = (this->now() - last_joy_time_).seconds() <= joy_timeout_sec_;
  auto velocity_command = std_msgs::msg::Float64MultiArray();
  velocity_command.data.resize(2, 0.0);
  auto position_command = trajectory_msgs::msg::JointTrajectory();
  position_command.joint_names = {"arm_1_joint", "arm_2_joint", "arm_3_joint", "arm_4_joint"};
  position_command.points.resize(1);
  auto & position_point = position_command.points.front();
  position_point.positions.resize(4, 0.0);
  position_point.time_from_start = rclcpp::Duration::from_seconds(arm_position_trajectory_duration_);

  if ((this->now() - last_health_time_).seconds() > 0.5) {
    position_hardware_healthy_.fill(false);
    resync_position_targets_.fill(true);
  }

  if (command_is_fresh) {
    velocity_command.data[0] = arm_y_velocity_command_;
    velocity_command.data[1] = gripper_velocity_command_;
    if (recovery_ready_ && arm_position_targets_initialized_) {
      arm_4_position_target_ += arm_4_axis_command_ * arm_4_position_rate_ * period_sec;
    }
    if (recovery_ready_ && selected_arm_123_joint_ >= 0 &&
      position_hardware_healthy_[selected_arm_123_joint_] &&
      !resync_position_targets_[selected_arm_123_joint_] &&
      position_sync_hold_[selected_arm_123_joint_] == 0) {
      arm_123_position_targets_[selected_arm_123_joint_] +=
        arm_123_axis_command_ * arm_123_position_rate_ * period_sec;
    }
    cmd_vel_publisher_->publish(last_cmd_vel_);
  } else {
    last_cmd_vel_ = geometry_msgs::msg::Twist();
    arm_y_velocity_command_ = 0.0;
    arm_4_axis_command_ = 0.0;
    arm_123_axis_command_ = 0.0;
    selected_arm_123_joint_ = -1;
    gripper_velocity_command_ = 0.0;
    cmd_vel_publisher_->publish(last_cmd_vel_);
  }

  arm_velocity_publisher_->publish(velocity_command);
  for (size_t index = 0; index < 3; ++index) {
    // Keep finite targets for all trajectory joints. Hardware gates unavailable motors.
    position_point.positions[index] = arm_123_position_targets_[index];
    if (position_sync_hold_[index] > 0) {--position_sync_hold_[index];}
  }
  position_point.positions[3] = arm_4_position_target_;
  bool synchronized = arm_position_targets_initialized_;
  for (size_t index = 0; index < 3; ++index) {
    if (position_hardware_healthy_[index] && resync_position_targets_[index]) {
      synchronized = false;
    }
  }
  if (recovery_ready_ && synchronized) {
    position_command.header.stamp = this->now();
    arm_position_publisher_->publish(position_command);
  }
}

void ManualController::joint_state_callback(const sensor_msgs::msg::JointState::SharedPtr msg)
{
  const auto find_position = [&msg](const std::string & name, double & position) {
      const auto it = std::find(msg->name.begin(), msg->name.end(), name);
      if (it == msg->name.end()) {return false;}
      const auto index = static_cast<size_t>(std::distance(msg->name.begin(), it));
      if (index >= msg->position.size() || !std::isfinite(msg->position[index])) {return false;}
      position = msg->position[index];
      return true;
    };
  double position = 0.0;
  if (!arm_position_targets_initialized_ && find_position("arm_4_joint", position)) {
    arm_4_position_target_ = position;
    arm_position_targets_initialized_ = true;
  }
  for (size_t index = 0; index < 3; ++index) {
    if (position_hardware_healthy_[index] && resync_position_targets_[index] &&
      find_position("arm_" + std::to_string(index + 1) + "_joint", position)) {
      arm_123_position_targets_[index] = position;
      resync_position_targets_[index] = false;
      position_sync_hold_[index] = 2;
    }
  }
}

void ManualController::health_callback(const std_msgs::msg::Float64MultiArray::SharedPtr msg)
{
  // RobStride order: arm_1, arm_2, arm_3; each has online/ready/fault.
  last_health_time_ = this->now();
  for (size_t index = 0; index < 3; ++index) {
    const bool healthy = msg->data.size() == 9 && msg->data[index * 3] > 0.5 &&
      msg->data[index * 3 + 1] > 0.5 && msg->data[index * 3 + 2] < 0.5;
    if (!healthy || !position_hardware_healthy_[index]) {
      resync_position_targets_[index] = true;
    }
    position_hardware_healthy_[index] = healthy;
  }
}

}  // namespace trc2026_manual

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(trc2026_manual::ManualController)
