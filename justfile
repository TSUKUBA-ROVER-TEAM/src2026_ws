set shell := ["bash", "-c"]

ros_distro := env_var('ROS_DISTRO')

default:
  @just --list

alias dep := deps
alias b := build
alias t := test
alias r := run
alias d := doc

_cd:
  @cd {{justfile_directory()}}

# check git status of all repositories
status: _cd
  find dep -type d -name .git -exec sh -c 'dir=$(dirname "{}"); echo "\033[34m=== $(basename "$dir") ===\033[m"; cd "$dir" && git status; echo ""' \;

# update repositories (git pull)
pull: _cd
  find dep -type d -name .git -exec sh -c 'cd "$(dirname "{}")" && git pull' \;

# install dependencies
deps: _cd
  vcs import --input build_depends.repos --recursive .
  sudo apt update
  rosdep update --rosdistro {{ros_distro}}
  rosdep install --from-paths . --ignore-src --rosdistro {{ros_distro}} -y

# build [packages...]
build *packages: _cd
    colcon build --symlink-install {{ if packages == "" { "" } else { "--packages-select " + packages } }}

# test [packages...]
test *packages: _cd
    source install/setup.bash && \
    if [ -z "{{packages}}" ]; then \
      dependency_packages="$(colcon list --base-paths dep --names-only)" && \
      package_args="${dependency_packages:+--packages-skip $dependency_packages}"; \
    else \
      package_args="--packages-select {{packages}}"; \
    fi && \
    colcon test $package_args --event-handlers console_direct+ --return-code-on-test-failure

# run launch file from bringup package
run name *args: _cd
    source install/setup.bash && \
    bringup_packages="$(colcon list --names-only | grep -E '_bringup$' || true)" && \
    bringup_count="$(printf '%s\n' "$bringup_packages" | sed '/^$/d' | wc -l)" && \
    if [ "$bringup_count" -eq 0 ]; then \
      echo "No package ending with '_bringup' found."; \
      exit 1; \
    elif [ "$bringup_count" -gt 1 ]; then \
      echo "Multiple bringup packages found:"; \
      printf '%s\n' "$bringup_packages"; \
      exit 1; \
    fi && \
    ros2 launch "$bringup_packages" {{name}}.launch.yaml {{args}}

# clean [packages...]
clean *packages: _cd
    {{ if packages == "" { "rm -rf build install log docs_build docs_output cross_reference" } else { "for pkg in " + packages + "; do rm -rf build/$pkg install/$pkg log/$pkg; done" } }}

# doc [packages...]
doc *packages: _cd
    source install/setup.bash && \
    colcon list {{ if packages != "" { "--packages-select " + packages } else { "" } }} --paths-only | xargs rosdoc2 build -p

# format [packages...]
format *packages: _cd
    if [ -z "{{packages}}" ]; then \
      pre-commit run --all-files || pre-commit run --all-files; \
    else \
      package_paths="$(colcon list --packages-select {{packages}} --paths-only)" && \
      files="$(printf '%s\n' "$package_paths" | xargs find -type f)" && \
      if [ -n "$files" ]; then \
        pre-commit run --files $files || pre-commit run --files $files; \
      fi; \
    fi
