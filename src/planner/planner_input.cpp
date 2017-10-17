#include <KrisLibrary/math/VectorTemplate.h>

#include "planner/planner_input.h"

bool PlannerMultiInput::load(const char* file){
  TiXmlDocument doc(file);
  TiXmlElement *root = GetRootNodeFromDocument(doc);
  return load(root);
}
bool PlannerMultiInput::load(TiXmlElement *node){
  CheckNodeName(node, "world");
  TiXmlElement* node_plannerinput = FindSubNode(node, "plannerinput");

  if(!node_plannerinput){
    std::cout << "world xml file has no plannerinput" << std::endl;
    return false;
  }


  //################################################################################
  // obtain default settings for all algorithms
  //################################################################################
  double max_planning_time;
  double epsilon_goalregion;
  double timestep_min;
  double timestep_max;
  TiXmlElement* node_timestep = FindSubNode(node_plannerinput, "timestep");
  if(node_timestep){
    GetStreamAttribute(node_timestep,"min") >> timestep_min;
    GetStreamAttribute(node_timestep,"max") >> timestep_max;
  }else{
    timestep_min= 0.01;
    timestep_max= 0.1;
  }
  TiXmlElement* node_max_planning_time = FindSubNode(node_plannerinput, "maxplanningtime");
  TiXmlElement* node_epsilon_goalregion = FindSubNode(node_plannerinput, "epsilongoalregion");

  GetStreamText(node_max_planning_time) >> max_planning_time;
  GetStreamText(node_epsilon_goalregion) >> epsilon_goalregion;

  //################################################################################
  // loop through all algorithms, search for individual settings; if not found
  // apply default settings 
  //################################################################################
  TiXmlElement* node_algorithm = FindFirstSubNode(node_plannerinput, "algorithm");

  while(node_algorithm!=NULL){
    PlannerInput* input = new PlannerInput();
    if(!input->load(node_plannerinput)) return false;

    GetStreamAttribute(node_algorithm,"name") >> input->name_algorithm;

    TiXmlElement* node_algorithm_max_planning_time = FindSubNode(node_algorithm, "maxplanningtime");
    TiXmlElement* node_algorithm_epsilon_goalregion = FindSubNode(node_algorithm, "epsilongoalregion");

    GetStreamTextDefaultDouble(node_algorithm_max_planning_time, max_planning_time) >> input->max_planning_time;
    GetStreamTextDefaultDouble(node_algorithm_epsilon_goalregion, epsilon_goalregion) >> input->epsilon_goalregion;

    TiXmlElement* node_algorithm_timestep = FindSubNode(node_algorithm, "timestep");
    if(node_algorithm_timestep){
      GetStreamAttributeDefaultDouble(node_algorithm_timestep,"min", timestep_min) >> input->timestep_min;
      GetStreamAttributeDefaultDouble(node_algorithm_timestep,"max", timestep_max) >> input->timestep_max;
    }else{
      input->timestep_min = timestep_min;
      input->timestep_max = timestep_max;
    }

    TiXmlElement* node_hierarchy = FindSubNode(node_algorithm, "hierarchy");
    uint level = 0;
    if(node_hierarchy){
      TiXmlElement* lindex = FindFirstSubNode(node_hierarchy, "level");
      while(lindex!=NULL){

        Layer layer;
        layer.level = level++;

        GetStreamAttribute(lindex, "inner_index") >> layer.inner_index;

        if(ExistStreamAttribute(lindex, "outer_index")){
          GetStreamAttribute(lindex, "outer_index") >> layer.outer_index;
          layer.isInnerOuter =true;
        }else{
          layer.outer_index = layer.inner_index;
          layer.isInnerOuter =false;
        }
        GetStreamAttribute(lindex, "type") >> layer.type;

        input->robot_idxs.push_back(layer.inner_index);
        input->layers.push_back(layer);

        lindex = FindNextSiblingNode(lindex, "level");
      }
    }

    inputs.push_back(input);
    node_algorithm = FindNextSiblingNode(node_algorithm, "algorithm");
  }
  for(uint k = 0; k < inputs.size(); k++){
    std::cout << inputs.at(k) << std::endl;
  }
  return true;
}

//################################################################################
///@brief get fixed general settings 
//################################################################################

bool PlannerInput::load(TiXmlElement *node)
{
  CheckNodeName(node, "plannerinput");
  TiXmlElement* node_plannerinput = node;

  if(!node_plannerinput){
    std::cout << "world xml file has no plannerinput" << std::endl;
    return false;
  }

  TiXmlElement* node_qinit = FindSubNode(node_plannerinput, "qinit");
  TiXmlElement* node_qgoal = FindSubNode(node_plannerinput, "qgoal");
  TiXmlElement* node_se3min = FindSubNode(node_plannerinput, "se3min");
  TiXmlElement* node_se3max = FindSubNode(node_plannerinput, "se3max");

  TiXmlElement* node_dqinit = FindSubNode(node_plannerinput, "dqinit");
  TiXmlElement* node_dqgoal = FindSubNode(node_plannerinput, "dqgoal");
  TiXmlElement* node_freeFloating = FindSubNode(node_plannerinput, "freeFloating");

  GetStreamAttribute(node_qinit,"config") >> q_init;
  GetStreamAttribute(node_qgoal,"config") >> q_goal;
  GetStreamAttribute(node_dqinit,"config") >> dq_init;
  GetStreamAttribute(node_dqgoal,"config") >> dq_goal;

  GetStreamText(node_freeFloating) >> freeFloating;

  GetStreamAttribute(node_se3min,"config")  >> se3min;
  GetStreamAttribute(node_se3max,"config")  >> se3max;

  TiXmlElement* node_robot = FindSubNode(node_plannerinput, "robot");
  if(node_robot){
    GetStreamText(node_robot) >> robot_idx;
  }else{
    robot_idx = 0;
  }

  return true;
}

bool PlannerInput::load(const char* file)
{
  TiXmlDocument doc(file);
  TiXmlElement *root = GetRootNodeFromDocument(doc);
  return load(root);
}
std::ostream& operator<< (std::ostream& out, const PlannerInput& pin) 
{
  out << std::string(80, '-') << std::endl;
  out << "[PlannerInput] " << pin.name_algorithm << std::endl;
  out << std::string(80, '-') << std::endl;
  out << "q_init             : " << pin.q_init << std::endl;
  out << "  dq_init          : " << pin.dq_init << std::endl;
  out << "q_goal             : " << pin.q_goal << std::endl;
  out << "  dq_goal          : " << pin.dq_goal << std::endl;
  out << "SE3_min            : " << pin.se3min << std::endl;
  out << "SE3_max            : " << pin.se3max << std::endl;
  out << "algorithm          : " << pin.name_algorithm << std::endl;
  out << "discr timestep     : [" << pin.timestep_min << "," << pin.timestep_max << "]" << std::endl;
  out << "max planning time  : " << pin.max_planning_time << " (seconds)" << std::endl;
  out << "epsilon_goalregion : " << pin.epsilon_goalregion << std::endl;
  out << "robot              : " << pin.robot_idx << std::endl;
  out << "robot indices      : ";
  for(uint k = 0; k < pin.robot_idxs.size(); k++){
    out << " " << pin.robot_idxs.at(k);
  }
  out << std::endl;
    
  out << std::string(80, '-') << std::endl;
  return out;
}
