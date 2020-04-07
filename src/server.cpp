#include <thread>
#include <ros/ros.h>
#include <move_base_msgs/MoveBaseAction.h>
#include <actionlib/server/simple_action_server.h>
#include <actionlib/client/simple_action_client.h>
#include <vacuumcleaner/cleaningAction.h>
#include "nav_msgs/OccupancyGrid.h"
#include <tf2_ros/transform_listener.h>

using MoveBaseClient = actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction>;
class CleaningAction
{
private:
  ros::NodeHandle _node_handle;
  actionlib::SimpleActionServer<vacuumcleaner::cleaningAction> _action_server;
  std::string _action_name;
  vacuumcleaner::cleaningFeedback _feedback;
  ros::Subscriber _map_subscriber; ///< Will subscribe to gmappping map updates.
  MoveBaseClient _move_base_client; ///< Will be used to send position goals
  vacuumcleaner::cleaningGoalConstPtr _goal; ///< received goal from actionlib client
  nav_msgs::OccupancyGrid _local_map; ///< Map from SLAM node updated with visited cells
  double _map_x = 0; //< x position in the map frame of robot
  double _map_y = 0; //< y position in the map frame of robot
  tf2_ros::Buffer _tf_buffer;
  tf2_ros::TransformListener _tf_listener;
  const uint64_t RADIUS = 7;
  struct Direction
  {
    int8_t x = 0;
    int8_t y = 0;
    bool IsSet()
    {
      return x != 0 and y != 0;
    }
  };
  Direction _direction; //< Direction robot is currently heading to
public:

  CleaningAction(std::string const& name) :
    _action_server(_node_handle, name,false),
    _action_name(name),
    _tf_listener(_tf_buffer),
    _move_base_client("move_base", true)
  {
    std::string map_topic("map");
    if (not _node_handle.getParam("map_topic", map_topic))
    {
      ROS_INFO_STREAM("No map topic provided, using default:\""<<map_topic<<"\"");
    }
    
    while(not _move_base_client.waitForServer(ros::Duration(5.0)))
    {
      ROS_INFO("Waiting for the move_base action server to come up");
    }
    _map_subscriber = _node_handle.subscribe(map_topic, 1000, &CleaningAction::OnMap, this);
    _action_server.registerGoalCallback([](){ROS_INFO_STREAM("goal callback: "<<std::this_thread::get_id());});
    _action_server.start();
  }

  /*
   * x = up
   * y = down
   */
  int8_t GetXY(int64_t x, int64_t y)
  {
    if (x < 0 or y < 0)
    {
      return -1;
    }

    if (x > _local_map.info.height or y > _local_map.info.width)
    {
      return -1;
    }

    //ROS_INFO_STREAM("accessing: [" << (map->info.width * y + x)<<"]"); 
    return _local_map.data[_local_map.info.width * y + x];
  }

  /*
   * x = up
   * y = down
   */
  int8_t GetRelative(int64_t x, int64_t y)
  {
    // current map tile
    uint64_t grid_x = (uint64_t)((_map_x - _local_map.info.origin.position.x) / _local_map.info.resolution);
    uint64_t grid_y = (uint64_t)((_map_y - _local_map.info.origin.position.y) / _local_map.info.resolution);
     
    //ROS_INFO_STREAM("current: "<< _map_x<<", "<< _map_y);
    //ROS_INFO_STREAM("grid: "<< grid_x<<", "<< grid_y);
    int64_t actual_x = grid_x + x;
    int64_t actual_y = grid_y + y;

    return GetXY(actual_x, actual_y);
  }
  int8_t Get(int64_t x, int64_t y)
  {
    // current map tile
    uint64_t grid_x = (uint64_t)((_map_x - _local_map.info.origin.position.x) / _local_map.info.resolution);
    uint64_t grid_y = (uint64_t)((_map_y - _local_map.info.origin.position.y) / _local_map.info.resolution);
    
    //transform from relative coords to _map coordinates
    //(0,0) is upper left corner.
    int64_t actual_x = grid_x + x - RADIUS/2 -1;
    int64_t actual_y = grid_y + y - RADIUS/2 -1;
    
    return GetXY(actual_x, actual_y);
  }
  void OnMoveGoalCompletion(const actionlib::SimpleClientGoalState& state,  const move_base_msgs::MoveBaseResultConstPtr& result) 
  {
    ROS_INFO_STREAM("Result"<< result); 
    SetDirection();
  }

  void SetDirection()
  {


    geometry_msgs::TransformStamped map_transform;
    try
    {
      map_transform = _tf_buffer.lookupTransform("map", "base_footprint", ros::Time(0));
      _map_x = map_transform.transform.translation.x;
      _map_y = map_transform.transform.translation.y;
     }
    catch (tf2::TransformException &ex) 
    {
      ROS_WARN("%s",ex.what());
    }
 

//    ROS_INFO_STREAM("START");
//    ROS_INFO_STREAM("[" << static_cast<int32_t>(GetRelative(-1, 1)) << ","<<static_cast<int32_t>(GetRelative(0, 1))<< ","<<static_cast<int32_t>(GetRelative(1, 1))<<"]"); 
//    ROS_INFO_STREAM("[" << static_cast<int32_t>(GetRelative(-1, 0)) << ","<<static_cast<int32_t>(GetRelative(0, 0))<< ","<<static_cast<int32_t>(GetRelative(1, 0))<<"]"); 
//    ROS_INFO_STREAM("[" << static_cast<int32_t>(GetRelative(-1, -1)) << ","<<static_cast<int32_t>(GetRelative(0, -1))<< ","<<static_cast<int32_t>(GetRelative(1, -1))<<"]");    
//    ROS_INFO_STREAM("END");
    //ROS_INFO_STREAM("choppa: "<< static_cast<int32_t>(GetRelative(0,0)));
    //    
    _direction.x = 1;
    _direction.y = 0;
    float translation_x = _direction.x * _local_map.info.resolution;
    float translation_y = _direction.y * _local_map.info.resolution;
    //ROS_INFO_STREAM("Current position in map frame: ("<<_map_x<<","<<_map_y<<")"); 
    move_base_msgs::MoveBaseGoal goal;
    goal.target_pose.header.frame_id = "map";
    goal.target_pose.header.stamp = ros::Time::now();
    goal.target_pose.pose.position.x = _map_x + translation_x;
    goal.target_pose.pose.position.y = _map_y + translation_y;
    goal.target_pose.pose.orientation.w = 1.0;
    //ROS_INFO_STREAM(goal); 
    _move_base_client.sendGoal(goal, [this](auto const& a, auto const& b){this->OnMoveGoalCompletion(a, b);});
 
  }

  void OnMap(nav_msgs::OccupancyGrid::ConstPtr const& new_map)
  { 
    ROS_INFO_STREAM("printing value "<<_local_map.info.width);

    if (_local_map.info.width != new_map->info.width or _local_map.info.height != new_map->info.height)
    {
      ROS_INFO_STREAM("Received uncompatible map. Resetting map.");
      _local_map = *new_map;
    }
    else
    {
      ROS_INFO_STREAM("Updating local map with new data.");
      for (int i = 0; i < _local_map.info.width * _local_map.info.height; i++)
      {
        if (_local_map.data[i] < new_map->data[i])
	{
	  _local_map.data[i] = new_map->data[i];
	}
      }
    }

    if (not _direction.IsSet())
    {
      SetDirection();
    }
    
   //ROS_INFO_STREAM("OnMap: "<<new_map->info); 
  }

  void OnGoal(vacuumcleaner::cleaningGoalConstPtr goal)
  {
    ROS_INFO_STREAM("OnGoal: "<<std::this_thread::get_id()); 
    _goal = _action_server.acceptNewGoal();
  }
};


int main(int argc, char** argv)
{
  ros::init(argc, argv, "cleaning");
  CleaningAction cleaningAction("cleaning");
  ros::spin();

  return 0;
}
