#include "planner.h"

MotionPlanner::MotionPlanner(RobotWorld *world, WorldSimulation *sim):
  _world(world),_sim(sim)
{
  _irobot = 0;
  _icontroller = 0;
}
const MultiPath& MotionPlanner::GetPath()
{
  return _path;
}
std::string MotionPlanner::getName(){
  return "Motion Planner";
}

void MotionPlanner::CheckFeasibility( Robot *robot, SingleRobotCSpace &cspace, Config &q){
  if(!cspace.IsFeasible(q)) {
    cout<<"configuration is infeasible, violated constraints:"<<endl;
    std::cout << q << std::endl;
    vector<bool> infeasible;
    cspace.CheckObstacles(q,infeasible);
    for(size_t i=0;i<infeasible.size();i++){
      //if(!infeasible[i]) cout<<"  OK"<<cspace.ObstacleName(i)<<endl;
      if(infeasible[i]){
        int icq = i - (int)robot->joints.size();
        cout<<"-->"<<cspace.ObstacleName(i) << " | pen-depth: ";
        cout << cspace.collisionQueries[icq].PenetrationDepth() << " | dist: ";
        cout << cspace.collisionQueries[icq].Distance(1e-3,1e-3) << std::endl;
      }
    }
    std::cout << cspace.collisionQueries.size() << std::endl;
    exit(0);
  }
}

bool MotionPlanner::PlanPath(){

}
bool MotionPlanner::solve(Config &p_init, Config &p_goal, double timelimit, bool shortcutting)
{

  this->_p_init = p_init;
  this->_p_goal = p_goal;
  this->_timelimit = timelimit;
  this->_shortcutting = shortcutting;
  Robot *robot = _world->robots[_irobot];
  robot->UpdateConfig(_p_init);
  util::SetSimulatedRobot(robot,*_sim,_p_init);
  this->_world->InitCollisions();

  std::cout << std::string(80, '-') << std::endl;

  std::cout << "Motion Planner:" << this->getName() << std::endl;
  std::cout << "p_init =" << p_init << std::endl;
  std::cout << "p_goal =" << p_goal << std::endl;

  std::cout << std::string(80, '-') << std::endl;


  //###########################################################################
  // Plan Path
  //###########################################################################

  WorldPlannerSettings settings;
  settings.InitializeDefault(*_world);
  //settings.robotSettings[0].contactEpsilon = 1e-2;
  //settings.robotSettings[0].contactIKMaxIters = 100;

  SingleRobotCSpace cspace = SingleRobotCSpace(*_world,_irobot,&settings);
  KinodynamicCSpaceSentinel kcspace;
  CheckFeasibility( robot, cspace, _p_init);
  CheckFeasibility( robot, cspace, _p_goal);


  MotionPlannerFactory factory;
  //factory.perturbationRadius = 0.1;
  //factory.type = "rrt";
  //factory.type = "prm";
  //factory.type = "sbl";
  //factory.type = "sblprt";
  //factory.type = "rrt*";
  //factory.type = "lazyrrg*";
  //factory.type = "fmm"; //warning: slows down system 

  //factory.type = "sbl";
  factory.type = "rrt";
  factory.shortcut = this->_shortcutting;

  SmartPointer<MotionPlannerInterface> planner = factory.Create(&cspace,_p_init,_p_goal);

  HaltingCondition cond;
  cond.foundSolution=true;
  cond.timeLimit = this->_timelimit;

  std::cout << "Start Planning" << std::endl;
  string res = planner->Plan(_milestone_path,cond);
  if(_milestone_path.edges.empty())
  {
   printf("Planning failed\n");
   this->_isSolved = false;
  }else{
   printf("Planning succeeded, path has length %g\n",_milestone_path.Length());
   this->_isSolved = true;
  }

  //###########################################################################
  // Time Optimize Path, Convert to MultiPath and Save Path
  //###########################################################################
  if(this->_isSolved){
    double dstep = 0.1;
    Config cur;
    vector<Config> keyframes;
    for(double d = 0; d <= 1; d+=dstep)
    {
      std::cout << d << std::endl;
      _milestone_path.Eval(d,cur);
      keyframes.push_back(cur);
    }
    _path.SetMilestones(keyframes);
    double xtol=0.01;
    double ttol=0.01;
    std::cout << "time optimizing" << std::endl;
    bool res=GenerateAndTimeOptimizeMultiPath(*robot,_path,xtol,ttol);


    std::string date = util::GetCurrentTimeString();
    string out = "../data/paths/path_"+date+".xml";
    _path.Save(out);
    std::cout << "saved path to "<<out << std::endl;
  }else{
    std::cout << "Planner did not find a solution" << std::endl;
  }
}

void MotionPlanner::SendCommandStringController(string cmd, string arg)
{
  if(!_sim->robotControllers[_icontroller]->SendCommand(cmd,arg)) {
    std::cout << std::string(80, '-') << std::endl;
    std::cout << "ERROR in controller commander" << std::endl;
    std::cout << cmd << " command  does not work with the robot's controller" << std::endl;
    std::cout << std::string(80, '-') << std::endl;
    throw "Controller command not supported!";
  }
}
bool MotionPlanner::SendToController()
{
  if(!_isSolved){ return false; }

  double dstep = 0.1;
  Config q;
  Config dq;

  for(double d = 0; d <= 1; d+=dstep)
  {
    _path.Evaluate(d, q, dq);
    stringstream qstr;
    qstr<<q<<dq;
    string cmd( (d<=0)?("set_qv"):("append_qv") );
    SendCommandStringController(cmd,qstr.str());
  }

  std::cout << "Sending Path to Controller" << std::endl;
  Robot *robot = _world->robots[_irobot];
  util::SetSimulatedRobot(robot,*_sim,_p_init);
  robot->UpdateConfig(_p_goal);
  std::cout << "Done Path to Controller" << std::endl;
  return true;
}
