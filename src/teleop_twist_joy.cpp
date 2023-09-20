/**
Software License Agreement (BSD)

\authors   Mike Purvis <mpurvis@clearpathrobotics.com>
\copyright Copyright (c) 2014, Clearpath Robotics, Inc., All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Clearpath Robotics nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WAR-
RANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, IN-
DIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cinttypes>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <rcutils/logging_macros.h>
#include <sensor_msgs/msg/joy.hpp>

#include "teleop_twist_joy/teleop_twist_joy.hpp"

#define ROS_INFO_NAMED RCUTILS_LOG_INFO_NAMED
#define ROS_INFO_COND_NAMED RCUTILS_LOG_INFO_EXPRESSION_NAMED

namespace teleop_twist_joy
{

/**
 * Internal members of class. This is the pimpl idiom, and allows more flexibility in adding
 * parameters later without breaking ABI compatibility, for robots which link TeleopTwistJoy
 * directly into base nodes.
 */
struct TeleopTwistJoy::Impl
{
  void joyCallback(const sensor_msgs::msg::Joy::SharedPtr joy);
  void sendCmdVelMsg(const sensor_msgs::msg::Joy::SharedPtr, const std::string& which_map);

  rclcpp::Subscription<sensor_msgs::msg::Joy>::SharedPtr joy_sub;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub;

  bool require_enable_button;
  bool autorun_flag;
  int64_t enable_button;
  int64_t enable_turbo_button;
  int64_t enable_autorun_button;
  int64_t autorun_buffer;

  std::map<std::string, int64_t> axis_linear_map;
  std::map<std::string, std::map<std::string, double>> scale_linear_map;

  std::map<std::string, int64_t> axis_angular_map;
  std::map<std::string, int64_t> axis_angular_adjustment_map;
  std::map<std::string, std::map<std::string, double>> scale_angular_map;

  float_t speed_x_max;

  bool sent_disable_msg;
};

/**
 * Constructs TeleopTwistJoy.
 */
TeleopTwistJoy::TeleopTwistJoy(const rclcpp::NodeOptions& options) : Node("teleop_twist_joy_node", options)
{
  pimpl_ = new Impl;

  pimpl_->cmd_vel_pub = this->create_publisher<geometry_msgs::msg::Twist>("cmd_vel", 10);
  pimpl_->joy_sub = this->create_subscription<sensor_msgs::msg::Joy>("joy", rclcpp::QoS(10),
    std::bind(&TeleopTwistJoy::Impl::joyCallback, this->pimpl_, std::placeholders::_1));

  pimpl_->require_enable_button = this->declare_parameter("require_enable_button", true);

  pimpl_->autorun_flag = false;

  pimpl_->enable_button = this->declare_parameter("enable_button", 5);

  pimpl_->enable_turbo_button = this->declare_parameter("enable_turbo_button", -1);

  pimpl_->enable_autorun_button = this->declare_parameter("enable_autorun_button", -1);

  pimpl_->autorun_buffer = 0;

  std::map<std::string, int64_t> default_linear_map{
    {"x", 5L},
    {"y", -1L},
    {"z", -1L},
  };
  this->declare_parameters("axis_linear", default_linear_map);
  this->get_parameters("axis_linear", pimpl_->axis_linear_map);

  std::map<std::string, int64_t> default_angular_map{
    {"yaw", 2L},
    {"pitch", -1L},
    {"roll", -1L},
  };
  this->declare_parameters("axis_angular", default_angular_map);
  this->get_parameters("axis_angular", pimpl_->axis_angular_map);

  std::map<std::string, int64_t> default_angular_adjustment_map{
      {"yaw", 3L},
          {"pitch", -1L},
          {"roll", -1L},
  };
  this->declare_parameters("axis_angular_adjustment", default_angular_adjustment_map);
  this->get_parameters("axis_angular_adjustment", pimpl_->axis_angular_adjustment_map);

  std::map<std::string, double> default_scale_linear_normal_map{
    {"x", 0.5},
    {"y", 0.0},
    {"z", 0.0},
  };
  this->declare_parameters("scale_linear", default_scale_linear_normal_map);
  this->get_parameters("scale_linear", pimpl_->scale_linear_map["normal"]);

  std::map<std::string, double> default_scale_linear_turbo_map{
    {"x", 1.0},
    {"y", 0.0},
    {"z", 0.0},
  };
  this->declare_parameters("scale_linear_turbo", default_scale_linear_turbo_map);
  this->get_parameters("scale_linear_turbo", pimpl_->scale_linear_map["turbo"]);

  // autorun scale
  std::map<std::string, double> default_scale_linear_autorun_map{
    {"x", 1.0},
    {"y", 0.0},
    {"z", 0.0},
  };
  this->declare_parameters("scale_linear_autorun", default_scale_linear_autorun_map);
  this->get_parameters("scale_linear_autorun", pimpl_->scale_linear_map["autorun"]);

  std::map<std::string, double> default_scale_angular_normal_map{
    {"yaw", 0.5},
    {"pitch", 0.0},
    {"roll", 0.0},
  };
  this->declare_parameters("scale_angular", default_scale_angular_normal_map);
  this->get_parameters("scale_angular", pimpl_->scale_angular_map["normal"]);

  std::map<std::string, double> default_scale_angular_turbo_map{
    {"yaw", 1.0},
    {"pitch", 0.0},
    {"roll", 0.0},
  };
  this->declare_parameters("scale_angular_turbo", default_scale_angular_turbo_map);
  this->get_parameters("scale_angular_turbo", pimpl_->scale_angular_map["turbo"]);

  // autorun scale
  std::map<std::string, double> default_scale_angular_autorun_map{
    {"yaw", 1.0},
    {"pitch", 0.0},
    {"roll", 0.0},
  };
  this->declare_parameters("scale_angular_autorun", default_scale_angular_autorun_map);
  this->get_parameters("scale_angular_autorun", pimpl_->scale_angular_map["autorun"]);

  ROS_INFO_COND_NAMED(pimpl_->require_enable_button, "TeleopTwistJoy",
      "Teleop enable button %" PRId64 ".", pimpl_->enable_button);
  ROS_INFO_COND_NAMED(pimpl_->enable_turbo_button >= 0, "TeleopTwistJoy",
    "Turbo on button %" PRId64 ".", pimpl_->enable_turbo_button);

  for (std::map<std::string, int64_t>::iterator it = pimpl_->axis_linear_map.begin();
       it != pimpl_->axis_linear_map.end(); ++it)
  {
    ROS_INFO_COND_NAMED(it->second != -1L, "TeleopTwistJoy", "Linear axis %s on %" PRId64 " at scale %f.",
      it->first.c_str(), it->second, pimpl_->scale_linear_map["normal"][it->first]);
    ROS_INFO_COND_NAMED(pimpl_->enable_turbo_button >= 0 && it->second != -1, "TeleopTwistJoy",
      "Turbo for linear axis %s is scale %f.", it->first.c_str(), pimpl_->scale_linear_map["turbo"][it->first]);
  }

  for (std::map<std::string, int64_t>::iterator it = pimpl_->axis_angular_map.begin();
       it != pimpl_->axis_angular_map.end(); ++it)
  {
    ROS_INFO_COND_NAMED(it->second != -1L, "TeleopTwistJoy", "Angular axis %s on %" PRId64 " at scale %f.",
      it->first.c_str(), it->second, pimpl_->scale_angular_map["normal"][it->first]);
    ROS_INFO_COND_NAMED(pimpl_->enable_turbo_button >= 0 && it->second != -1, "TeleopTwistJoy",
      "Turbo for angular axis %s is scale %f.", it->first.c_str(), pimpl_->scale_angular_map["turbo"][it->first]);
  }

  pimpl_->speed_x_max = 0;

  pimpl_->sent_disable_msg = false;

  auto param_callback =
  [this](std::vector<rclcpp::Parameter> parameters)
  {
    static std::set<std::string> intparams = {"axis_linear.x", "axis_linear.y", "axis_linear.z",
                                              "axis_angular.yaw", "axis_angular.pitch", "axis_angular.roll",
                                              "axis_angular_adjustment.yaw", "axis_angular_adjustment.pitch", "axis_angular_adjustment.roll",
                                              "enable_button", "enable_turbo_button", "enable_autorun_button"};
    static std::set<std::string> doubleparams = {"scale_linear.x", "scale_linear.y", "scale_linear.z",
                                                 "scale_linear_turbo.x", "scale_linear_turbo.y", "scale_linear_turbo.z",
                                                 "scale_linear_autorun.x", "scale_linear_autorun.y", "scale_linear_autorun.z",
                                                 "scale_angular.yaw", "scale_angular.pitch", "scale_angular.roll",
                                                 "scale_angular_turbo.yaw", "scale_angular_turbo.pitch", "scale_angular_turbo.roll",
                                                 "scale_angular_autorun.yaw", "scale_angular_autorun.pitch", "scale_angular_autorun.roll"};
    static std::set<std::string> boolparams = {"require_enable_button"};
    auto result = rcl_interfaces::msg::SetParametersResult();
    result.successful = true;

    // Loop to check if changed parameters are of expected data type
    for(const auto & parameter : parameters)
    {
      if (intparams.count(parameter.get_name()) == 1)
      {
        if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_INTEGER)
        {
          result.reason = "Only integer values can be set for '" + parameter.get_name() + "'.";
          RCLCPP_WARN(this->get_logger(), result.reason.c_str());
          result.successful = false;
          return result;
        }
      }
      else if (doubleparams.count(parameter.get_name()) == 1)
      {
        if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_DOUBLE)
        {
          result.reason = "Only double values can be set for '" + parameter.get_name() + "'.";
          RCLCPP_WARN(this->get_logger(), result.reason.c_str());
          result.successful = false;
          return result;
        }
      }
      else if (boolparams.count(parameter.get_name()) == 1)
      {
        if (parameter.get_type() != rclcpp::ParameterType::PARAMETER_BOOL)
        {
          result.reason = "Only boolean values can be set for '" + parameter.get_name() + "'.";
          RCLCPP_WARN(this->get_logger(), result.reason.c_str());
          result.successful = false;
          return result;
        }
      }
    }

    // Loop to assign changed parameters to the member variables
    for (const auto & parameter : parameters)
    {
      if (parameter.get_name() == "require_enable_button")
      {
        this->pimpl_->require_enable_button = parameter.get_value<rclcpp::PARAMETER_BOOL>();
      }
      if (parameter.get_name() == "enable_button")
      {
        this->pimpl_->enable_button = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "enable_turbo_button")
      {
        this->pimpl_->enable_turbo_button = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "enable_autorun_button")
      {
          this->pimpl_->enable_autorun_button = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_linear.x")
      {
        this->pimpl_->axis_linear_map["x"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_linear.y")
      {
        this->pimpl_->axis_linear_map["y"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_linear.z")
      {
        this->pimpl_->axis_linear_map["z"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_angular_adjustment.yaw")
      {
          this->pimpl_->axis_angular_adjustment_map["yaw"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_angular_adjustment.pitch")
      {
          this->pimpl_->axis_angular_adjustment_map["pitch"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_angular_adjustment.roll")
      {
          this->pimpl_->axis_angular_adjustment_map["roll"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_angular.yaw")
      {
        this->pimpl_->axis_angular_map["yaw"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_angular.pitch")
      {
        this->pimpl_->axis_angular_map["pitch"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "axis_angular.roll")
      {
        this->pimpl_->axis_angular_map["roll"] = parameter.get_value<rclcpp::PARAMETER_INTEGER>();
      }
      else if (parameter.get_name() == "scale_linear_autorun.x")
      {
        this->pimpl_->scale_linear_map["autorun"]["x"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear_autorun.y")
      {
        this->pimpl_->scale_linear_map["autorun"]["y"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear_autorun.z")
      {
        this->pimpl_->scale_linear_map["autorun"]["z"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear_turbo.x")
      {
        this->pimpl_->scale_linear_map["turbo"]["x"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear_turbo.y")
      {
        this->pimpl_->scale_linear_map["turbo"]["y"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear_turbo.z")
      {
        this->pimpl_->scale_linear_map["turbo"]["z"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear.x")
      {
        this->pimpl_->scale_linear_map["normal"]["x"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear.y")
      {
        this->pimpl_->scale_linear_map["normal"]["y"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_linear.z")
      {
        this->pimpl_->scale_linear_map["normal"]["z"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular_autorun.yaw")
      {
        this->pimpl_->scale_angular_map["autorun"]["yaw"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular_autorun.pitch")
      {
        this->pimpl_->scale_angular_map["autorun"]["pitch"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular_autorun.roll")
      {
        this->pimpl_->scale_angular_map["autorun"]["roll"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular_turbo.yaw")
      {
        this->pimpl_->scale_angular_map["turbo"]["yaw"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular_turbo.pitch")
      {
        this->pimpl_->scale_angular_map["turbo"]["pitch"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular_turbo.roll")
      {
        this->pimpl_->scale_angular_map["turbo"]["roll"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular.yaw")
      {
        this->pimpl_->scale_angular_map["normal"]["yaw"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular.pitch")
      {
        this->pimpl_->scale_angular_map["normal"]["pitch"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
      else if (parameter.get_name() == "scale_angular.roll")
      {
        this->pimpl_->scale_angular_map["normal"]["roll"] = parameter.get_value<rclcpp::PARAMETER_DOUBLE>();
      }
    }
    return result;
  };

  callback_handle = this->add_on_set_parameters_callback(param_callback);
}

TeleopTwistJoy::~TeleopTwistJoy()
{
  delete pimpl_;
}

double getVal(const sensor_msgs::msg::Joy::SharedPtr joy_msg, const std::map<std::string, int64_t>& axis_map,
              const std::map<std::string, double>& scale_map, const std::string& fieldname)
{
    // fieldname が axis_map のどのインデックスにあるか 
  if (axis_map.find(fieldname) == axis_map.end() ||
      axis_map.at(fieldname) == -1L ||
      scale_map.find(fieldname) == scale_map.end() ||
      static_cast<int>(joy_msg->axes.size()) <= axis_map.at(fieldname)) // 
  {
    return 0.0;
  }

  return joy_msg->axes[axis_map.at(fieldname)] * scale_map.at(fieldname);
}

void TeleopTwistJoy::Impl::sendCmdVelMsg(const sensor_msgs::msg::Joy::SharedPtr joy_msg,
                                         const std::string& which_map)
{
  // Initializes with zeros by default.
  auto cmd_vel_msg = std::make_unique<geometry_msgs::msg::Twist>();
  float_t speed_x_temporary = getVal(joy_msg, axis_linear_map, scale_linear_map[which_map], "x");
  float_t speed_yaw_temporary = getVal(joy_msg, axis_angular_map, scale_angular_map[which_map], "yaw");

  if(this->autorun_flag)
  {
      // 直進方向の値を更新
      float_t limit;
      // x の値の最大値を決定
      limit = 1.0f * scale_linear_map["autorun"]["x"];
      // 十字キーの値を / 10 してから足す
      this->speed_x_max += (float_t) speed_x_temporary / 10;
      // 出力値が最大値を超えないように制限
      this->speed_x_max = this->speed_x_max >  limit ?  limit : this->speed_x_max;
      this->speed_x_max = this->speed_x_max < -limit ? -limit : this->speed_x_max;
      // 出力値の決定
      cmd_vel_msg->linear.x = this->speed_x_max;

      // 角度方向の値を更新
      float_t joystick = getVal(joy_msg, axis_angular_adjustment_map, scale_angular_map[which_map], "yaw");
      // Joystick の値と十字キーの値を加算
      float_t sum = (speed_yaw_temporary + joystick);
      // yaw の値の最大値を決定 ( 十字キーの最大値で制限 )
      limit = 1.0f * scale_angular_map["autorun"]["yaw"];
      // Joystick と十字キーを加算した値が最大値の範囲に収まるように制限
      sum = sum >  limit ?  limit : sum;
      sum = sum < -limit ? -limit : sum;
      // 出力値の決定
      cmd_vel_msg->angular.z = sum;
  }
  else
  {
      cmd_vel_msg->linear.x = speed_x_temporary;
      cmd_vel_msg->angular.z = speed_yaw_temporary;
  }

  cmd_vel_msg->linear.y = getVal(joy_msg, axis_linear_map, scale_linear_map[which_map], "y");
  cmd_vel_msg->linear.z = getVal(joy_msg, axis_linear_map, scale_linear_map[which_map], "z");
  cmd_vel_msg->angular.y = getVal(joy_msg, axis_angular_map, scale_angular_map[which_map], "pitch");
  cmd_vel_msg->angular.x = getVal(joy_msg, axis_angular_map, scale_angular_map[which_map], "roll");

  cmd_vel_pub->publish(std::move(cmd_vel_msg));
  sent_disable_msg = false;
}

void TeleopTwistJoy::Impl::joyCallback(const sensor_msgs::msg::Joy::SharedPtr joy_msg)
{
    if(enable_autorun_button >= 0 && static_cast<int>(joy_msg->buttons.size()) > enable_autorun_button)
    {
        auto autorun_button = joy_msg->buttons[enable_autorun_button];
        if(autorun_button - this->autorun_buffer > 0)
        {
            this->autorun_flag = this->autorun_flag ? false : true;
        }
        this->autorun_buffer = autorun_button;
    }

    RCLCPP_INFO(rclcpp::get_logger("joy_callback_logger"), "B : %d, Flag : %d, sent_disable_msg : %d", joy_msg->buttons[enable_autorun_button], this->autorun_flag ? 1 : 0, sent_disable_msg ? 1 : 0);

    if(!autorun_flag)
    {
        // 直進速度をリセット
        this->speed_x_max = 0;
    }

    if(autorun_flag)
    {
        sendCmdVelMsg(joy_msg, "autorun");
    }
    else if(enable_turbo_button >= 0 &&
                static_cast<int>(joy_msg->buttons.size()) > enable_turbo_button &&
                joy_msg->buttons[enable_turbo_button])
    {
        sendCmdVelMsg(joy_msg, "turbo");
    }
    else if (!require_enable_button ||
            (static_cast<int>(joy_msg->buttons.size()) > enable_button &&
             joy_msg->buttons[enable_button]))
    {
        sendCmdVelMsg(joy_msg, "normal");
    }
    else
    {
        // RCLCPP_INFO(rclcpp::get_logger("joy_callback_logger"), "Stop The Robot : %d", !sent_disable_msg ? 1 : 0);
        // When enable button is released, immediately send a single no-motion command
        // in order to stop the robot.
        if (!sent_disable_msg)
        {
            // Initializes with zeros by default.
            auto cmd_vel_msg = std::make_unique<geometry_msgs::msg::Twist>();
            cmd_vel_pub->publish(std::move(cmd_vel_msg));
            sent_disable_msg = true;
        }
    }
}
}  // namespace teleop_twist_joy

RCLCPP_COMPONENTS_REGISTER_NODE(teleop_twist_joy::TeleopTwistJoy)

