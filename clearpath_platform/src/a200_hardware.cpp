/**
*
*  \author     Paul Bovbel <pbovbel@clearpathrobotics.com>
*  \copyright  Copyright (c) 2014-2015, Clearpath Robotics, Inc.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name of Clearpath Robotics, Inc. nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL CLEARPATH ROBOTICS, INC. BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* Please send comments, questions, or patches to code@clearpathrobotics.com
*
*/

#include "clearpath_platform/a200_hardware.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"

namespace
{
  const uint8_t LEFT = 0, RIGHT = 1;
}

namespace
{
  const int UNDERVOLT_ERROR = 18;
  const int UNDERVOLT_WARN = 19;
  const int OVERVOLT_ERROR = 30;
  const int OVERVOLT_WARN = 29;
  const int DRIVER_OVERTEMP_ERROR = 50;
  const int DRIVER_OVERTEMP_WARN = 30;
  const int MOTOR_OVERTEMP_ERROR = 80;
  const int MOTOR_OVERTEMP_WARN = 70;
  const double LOWPOWER_ERROR = 0.2;
  const double LOWPOWER_WARN = 0.3;
  const int CONTROLFREQ_WARN = 90;
  const unsigned int SAFETY_TIMEOUT = 0x1;
  const unsigned int SAFETY_LOCKOUT = 0x2;
  const unsigned int SAFETY_ESTOP = 0x8;
  const unsigned int SAFETY_CCI = 0x10;
  const unsigned int SAFETY_PSU = 0x20;
  const unsigned int SAFETY_CURRENT = 0x40;
  const unsigned int SAFETY_WARN = (SAFETY_TIMEOUT | SAFETY_CCI | SAFETY_PSU);
  const unsigned int SAFETY_ERROR = (SAFETY_LOCKOUT | SAFETY_ESTOP | SAFETY_CURRENT);
}  // namespace

namespace clearpath_platform
{
  static const std::string HW_NAME = "A200Hardware";
  static const std::string LEFT_CMD_JOINT_NAME = "front_left_wheel_joint";
  static const std::string RIGHT_CMD_JOINT_NAME = "front_right_wheel_joint";

  /**
  * Get current encoder travel offsets from MCU and bias future encoder readings against them
  */
  void A200Hardware::resetTravelOffset()
  {
    horizon_legacy::Channel<clearpath::DataEncoders>::Ptr enc =
        horizon_legacy::Channel<clearpath::DataEncoders>::requestData(polling_timeout_);
    if (enc)
    {
      for (auto i = 0u; i < hw_states_position_offset_.size(); i++)
      {
        hw_states_position_offset_[i] = linearToAngular(enc->getTravel(isLeft(info_.joints[i].name)));
      }
    }
    else
    {
      RCLCPP_FATAL(
        rclcpp::get_logger(HW_NAME), "Could not get encoder data to calibrate travel offset");
    }
  }

  /**
  * Husky reports travel in metres, need radians for ros_control RobotHW
  */
  double A200Hardware::linearToAngular(const double &travel) const
  {
    return (travel / wheel_diameter_ * 2.0f);
  }

  /**
  * RobotHW provides velocity command in rad/s, Husky needs m/s,
  */
  double A200Hardware::angularToLinear(const double &angle) const
  {
    return (angle * wheel_diameter_ / 2.0f);
  }

  void A200Hardware::writeCommandsToHardware()
  {
    double diff_speed_left = angularToLinear(hw_commands_[left_cmd_joint_index_]);
    double diff_speed_right = angularToLinear(hw_commands_[right_cmd_joint_index_]);

    limitDifferentialSpeed(diff_speed_left, diff_speed_right);

    horizon_legacy::controlSpeed(diff_speed_left, diff_speed_right, max_accel_, max_accel_);
  }

  void A200Hardware::limitDifferentialSpeed(double &diff_speed_left, double &diff_speed_right)
  {
    double large_speed = std::max(std::abs(diff_speed_left), std::abs(diff_speed_right));

    if (large_speed > max_speed_)
    {
      diff_speed_left *= max_speed_ / large_speed;
      diff_speed_right *= max_speed_ / large_speed;
    }
  }


  /**
  * Pull latest speed and travel measurements from MCU, and store in joint structure for ros_control
  */
  void A200Hardware::updateJointsFromHardware()
  {

    horizon_legacy::Channel<clearpath::DataEncoders>::Ptr enc =
      horizon_legacy::Channel<clearpath::DataEncoders>::requestData(polling_timeout_);
    if (enc)
    {
      RCLCPP_DEBUG(
        rclcpp::get_logger(HW_NAME),
        "Received linear distance information (L: %f, R: %f)",
        enc->getTravel(LEFT), enc->getTravel(RIGHT));

      for (auto i = 0u; i < hw_states_position_.size(); i++)
      {
        double delta = linearToAngular(enc->getTravel(isLeft(info_.joints[i].name)))
            - hw_states_position_[i] - hw_states_position_offset_[i];

        // detect suspiciously large readings, possibly from encoder rollover
        if (std::abs(delta) < 1.0f)
        {
          hw_states_position_[i] += delta;
        }
        else
        {
          // suspicious! drop this measurement and update the offset for subsequent readings
          hw_states_position_offset_[i] += delta;
          RCLCPP_WARN(
            rclcpp::get_logger(HW_NAME),"Dropping overflow measurement from encoder");
        }
      }
    }
    else
    {
      RCLCPP_ERROR(
        rclcpp::get_logger(HW_NAME), "Could not get encoder data");
    }

    horizon_legacy::Channel<clearpath::DataDifferentialSpeed>::Ptr speed =
      horizon_legacy::Channel<clearpath::DataDifferentialSpeed>::requestData(polling_timeout_);
    if (speed)
    {
      RCLCPP_DEBUG(
        rclcpp::get_logger(HW_NAME),
        "Received linear speed information (L: %f, R: %f)",
        speed->getLeftSpeed(), speed->getRightSpeed());

      for (auto i = 0u; i < hw_states_velocity_.size(); i++)
      {
        if (isLeft(info_.joints[i].name) == LEFT)
        {
          hw_states_velocity_[i] = linearToAngular(speed->getLeftSpeed());
        }
        else
        { // assume RIGHT
          hw_states_velocity_[i] = linearToAngular(speed->getRightSpeed());
        }
      }
    }
        else
    {
      RCLCPP_ERROR(
        rclcpp::get_logger(HW_NAME), "Could not get speed data");
    }
  }

  /**
  * Pull latest status date from MCU.
  */
  void A200Hardware::readStatusFromHardware()
  {

    auto safety_status =
      horizon_legacy::Channel<clearpath::DataSafetySystemStatus>::requestData(polling_timeout_);
    if (safety_status)
    {
      uint16_t flags = safety_status->getFlags();
      
      // status_msg_.timeout = (flags & SAFETY_TIMEOUT) > 0;
      // status_msg_.lockout = (flags & SAFETY_LOCKOUT) > 0;
      // status_msg_.ros_pause = (flags & SAFETY_CCI) > 0;
      // status_msg_.no_battery = (flags & SAFETY_PSU) > 0;
      // status_msg_.current_limit = (flags & SAFETY_CURRENT) > 0;
      stop_msg_.data = (flags & SAFETY_ESTOP) > 0;
      power_msg_.battery_connected = static_cast<int8_t>(!((flags & SAFETY_PSU) > 0));
    }
    else
    {
      RCLCPP_ERROR(
        rclcpp::get_logger(HW_NAME), "Could not get safety_status");
    }


    auto system_status =
      horizon_legacy::Channel<clearpath::DataSystemStatus>::requestData(polling_timeout_);
    if (system_status)
    {
      // status_msg_.mcu_uptime = system_status->getUptime();

      power_msg_.shore_power_connected = clearpath_platform_msgs::msg::Power::NOT_APPLICABLE;
      power_msg_.power_12v_user_nominal = clearpath_platform_msgs::msg::Power::NOT_APPLICABLE;
      power_msg_.charging_complete  = clearpath_platform_msgs::msg::Power::NOT_APPLICABLE;

      power_msg_.measured_voltages[clearpath_platform_msgs::msg::Power::A200_BATTERY_VOLTAGE] = system_status->getVoltage(0);
      power_msg_.measured_voltages[clearpath_platform_msgs::msg::Power::A200_LEFT_DRIVER_VOLTAGE] = system_status->getVoltage(1);
      power_msg_.measured_voltages[clearpath_platform_msgs::msg::Power::A200_RIGHT_DRIVER_VOLTAGE] = system_status->getVoltage(2);

      power_msg_.measured_currents[clearpath_platform_msgs::msg::Power::A200_MCU_AND_USER_PORT_CURRENT] = system_status->getCurrent(0);
      power_msg_.measured_currents[clearpath_platform_msgs::msg::Power::A200_LEFT_DRIVER_CURRENT] = system_status->getCurrent(1);
      power_msg_.measured_currents[clearpath_platform_msgs::msg::Power::A200_RIGHT_DRIVER_CURRENT] = system_status->getCurrent(2);

      driver_left_temp_msg_.data = system_status->getTemperature(0);
      driver_right_temp_msg_.data = system_status->getTemperature(1);
      motor_left_temp_msg_.data = system_status->getTemperature(2);
      motor_right_temp_msg_.data = system_status->getTemperature(3);
    }
    else
    {
      RCLCPP_ERROR(
        rclcpp::get_logger(HW_NAME), "Could not get system_status");
    }

    status_node_->publish_status(status_msg_);
    status_node_->publish_power(power_msg_);
    status_node_->publish_stop_state(stop_msg_);
    status_node_->publish_temps(driver_left_temp_msg_, driver_right_temp_msg_, motor_left_temp_msg_, motor_right_temp_msg_);
  }



  /**
  * Determines if the joint is left or right based on the joint name
  */
  uint8_t A200Hardware::isLeft(const std::string &str)
  {
    if (str.find("left") != std::string::npos)
    {
      return LEFT;
    }
    return RIGHT;
  }


hardware_interface::CallbackReturn A200Hardware::on_init(const hardware_interface::HardwareInfo & info)
{
  if (hardware_interface::SystemInterface::on_init(info) != hardware_interface::CallbackReturn::SUCCESS)
  {
    return hardware_interface::CallbackReturn::ERROR;
  }

  RCLCPP_INFO(rclcpp::get_logger(HW_NAME), "Name: %s", info_.name.c_str());

  RCLCPP_INFO(rclcpp::get_logger(HW_NAME), "Number of Joints %zu", info_.joints.size());

  hw_states_position_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_states_position_offset_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_states_velocity_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_commands_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());

  wheel_diameter_ = std::stod(info_.hardware_parameters["wheel_diameter"]);
  max_accel_ = std::stod(info_.hardware_parameters["max_accel"]);
  max_speed_ = std::stod(info_.hardware_parameters["max_speed"]);
  polling_timeout_ = std::stod(info_.hardware_parameters["polling_timeout"]);

  serial_port_ = info_.hardware_parameters["serial_port"];

  status_node_ = std::make_shared<a200_status::A200Status>();
  // Resize the message to fix the platform model A200
  power_msg_.measured_voltages.resize(clearpath_platform_msgs::msg::Power::A200_VOLTAGES_SIZE);
  power_msg_.measured_currents.resize(clearpath_platform_msgs::msg::Power::A200_CURRENTS_SIZE);

  RCLCPP_INFO(rclcpp::get_logger(HW_NAME), "Port: %s", serial_port_.c_str());
  horizon_legacy::connect(serial_port_);
  horizon_legacy::configureLimits(max_speed_, max_accel_);
  resetTravelOffset();

  for (const hardware_interface::ComponentInfo & joint : info_.joints)
  {
    // A200Hardware has exactly two states and one command interface on each joint
    if (joint.command_interfaces.size() != 1)
    {
      RCLCPP_FATAL(
        rclcpp::get_logger(HW_NAME),
        "Joint '%s' has %zu command interfaces found. 1 expected.", joint.name.c_str(),
        joint.command_interfaces.size());
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.command_interfaces[0].name != hardware_interface::HW_IF_VELOCITY)
    {
      RCLCPP_FATAL(
        rclcpp::get_logger(HW_NAME),
        "Joint '%s' have %s command interfaces found. '%s' expected.", joint.name.c_str(),
        joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_VELOCITY);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces.size() != 2)
    {
      RCLCPP_FATAL(
        rclcpp::get_logger(HW_NAME),
        "Joint '%s' has %zu state interface. 2 expected.", joint.name.c_str(),
        joint.state_interfaces.size());
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces[0].name != hardware_interface::HW_IF_POSITION)
    {
      RCLCPP_FATAL(
        rclcpp::get_logger(HW_NAME),
        "Joint '%s' have '%s' as first state interface. '%s' expected.",
        joint.name.c_str(), joint.state_interfaces[0].name.c_str(),
        hardware_interface::HW_IF_POSITION);
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (joint.state_interfaces[1].name != hardware_interface::HW_IF_VELOCITY)
    {
      RCLCPP_FATAL(
        rclcpp::get_logger(HW_NAME),
        "Joint '%s' have '%s' as second state interface. '%s' expected.", joint.name.c_str(),
        joint.state_interfaces[1].name.c_str(), hardware_interface::HW_IF_VELOCITY);
      return hardware_interface::CallbackReturn::ERROR;
    }
  }

  return hardware_interface::CallbackReturn::SUCCESS;
}

std::vector<hardware_interface::StateInterface> A200Hardware::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> state_interfaces;
  for (auto i = 0u; i < info_.joints.size(); i++)
  {
    state_interfaces.emplace_back(hardware_interface::StateInterface(
      info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_states_position_[i]));
    state_interfaces.emplace_back(hardware_interface::StateInterface(
      info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &hw_states_velocity_[i]));
  }

  return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> A200Hardware::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> command_interfaces;

  for (auto i = 0u; i < info_.joints.size(); i++)
  {
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
      info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &hw_commands_[i]));

    // Determine which joints will be used for commands since Husky only has two motors
    if (info_.joints[i].name == LEFT_CMD_JOINT_NAME)
    {
      left_cmd_joint_index_ = i;
    }

    if (info_.joints[i].name == RIGHT_CMD_JOINT_NAME)
    {
      right_cmd_joint_index_ = i;
    }
  }

  return command_interfaces;
}

hardware_interface::CallbackReturn A200Hardware::on_activate(const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_INFO(rclcpp::get_logger(HW_NAME), "Starting ...please wait...");

  // set some default values
  for (auto i = 0u; i < hw_states_position_.size(); i++)
  {
    if (std::isnan(hw_states_position_[i]))
    {
      hw_states_position_[i] = 0;
      hw_states_position_offset_[i] = 0;
      hw_states_velocity_[i] = 0;
      hw_commands_[i] = 0;
    }
  }

  RCLCPP_INFO(rclcpp::get_logger(HW_NAME), "System Successfully started!");

  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::CallbackReturn A200Hardware::on_deactivate(const rclcpp_lifecycle::State & /*previous_state*/)
{
  RCLCPP_INFO(rclcpp::get_logger(HW_NAME), "Stopping ...please wait...");

  RCLCPP_INFO(rclcpp::get_logger(HW_NAME), "System successfully stopped!");

  return hardware_interface::CallbackReturn::SUCCESS;
}

hardware_interface::return_type A200Hardware::read(const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
{
  RCLCPP_DEBUG(rclcpp::get_logger(HW_NAME), "Reading from hardware");

  updateJointsFromHardware();

  RCLCPP_DEBUG(rclcpp::get_logger(HW_NAME), "Joints successfully read!");

  // This will run at 10Hz but status data is only needed at 1Hz.
  static int i = 0;
  if (i <= 10)
  {
    i++;
  }
  else
  {
    readStatusFromHardware();
    i = 0;
  }

  return hardware_interface::return_type::OK;
}

hardware_interface::return_type A200Hardware::write(const rclcpp::Time & /*time*/, const rclcpp::Duration & /*period*/)
{
  RCLCPP_DEBUG(rclcpp::get_logger(HW_NAME), "Writing to hardware");

  writeCommandsToHardware();

  RCLCPP_DEBUG(rclcpp::get_logger(HW_NAME), "Joints successfully written!");

  return hardware_interface::return_type::OK;
}

}  // namespace clearpath_platform

#include "pluginlib/class_list_macros.hpp"
PLUGINLIB_EXPORT_CLASS(
  clearpath_platform::A200Hardware, hardware_interface::SystemInterface)
