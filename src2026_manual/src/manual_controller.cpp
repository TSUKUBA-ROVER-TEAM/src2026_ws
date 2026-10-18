#include "trc2026_manual/manual_controller.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace trc2026_manual
{

ManualController::ManualController(const rclcpp::NodeOptions & options)
: BaseManualController("manual_controller_node", options)
{
  cmd_vel_publisher_ = this->create_publisher<geometry_msgs::msg::Twist>("cmd_vel", 1);
  arm_velocity_publisher_ = this->create_publisher<std_msgs::msg::Float64MultiArray>(
    "/arm_velocity_controller/commands", 1);
  arm_position_publisher_ = this->create_publisher<std_msgs::msg::Float64MultiArray>(
    "/arm_position_controller/commands", 1);

  this->declare_parameter("axis_linear_x", 4);
  this->declare_parameter("axis_linear_y", 3);
  this->declare_parameter("axis_angular_z", 0);
  this->declare_parameter("scale_linear_x", 1.0);
  this->declare_parameter("scale_linear_y", 1.0);
  this->declare_parameter("scale_angular_z", 1.0);
  this->declare_parameter("arm_y_axis", 6);
  this->declare_parameter("arm_4_positive_axis", 2);
  this->declare_parameter("arm_4_negative_axis", 5);
  this->declare_parameter("gripper_positive_button", 4);
  this->declare_parameter("gripper_negative_button", 5);
  this->declare_parameter("arm_y_velocity_scale", 20000.0);
  this->declare_parameter("arm_4_position_rate", 10.0);
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
  this->get_parameter("gripper_positive_button", gripper_positive_button_);
  this->get_parameter("gripper_negative_button", gripper_negative_button_);
  this->get_parameter("arm_y_velocity_scale", arm_y_velocity_scale_);
  this->get_parameter("arm_4_position_rate", arm_4_position_rate_);
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
  auto position_command = std_msgs::msg::Float64MultiArray();
  position_command.data.resize(1, 0.0);

  if (command_is_fresh) {
    velocity_command.data[0] = arm_y_velocity_command_;
    velocity_command.data[1] = gripper_velocity_command_;
    arm_4_position_target_ += arm_4_axis_command_ * arm_4_position_rate_ * period_sec;
    position_command.data[0] = arm_4_position_target_;
    cmd_vel_publisher_->publish(last_cmd_vel_);
  } else {
    last_cmd_vel_ = geometry_msgs::msg::Twist();
    arm_y_velocity_command_ = 0.0;
    arm_4_axis_command_ = 0.0;
    gripper_velocity_command_ = 0.0;
    cmd_vel_publisher_->publish(last_cmd_vel_);
    position_command.data[0] = arm_4_position_target_;
  }

  arm_velocity_publisher_->publish(velocity_command);
  arm_position_publisher_->publish(position_command);
}

}  // namespace trc2026_manual

#include "rclcpp_components/register_node_macro.hpp"
RCLCPP_COMPONENTS_REGISTER_NODE(trc2026_manual::ManualController)
