#include "pathspace_atomic.h"
#include "gui/drawMotionPlanner.h"

PathSpaceAtomic::PathSpaceAtomic(RobotWorld *world_, PathSpaceInput* input_):
  PathSpace(world_, input_)
{
}

bool PathSpaceAtomic::isAtomic() const{
  return true;
}

std::vector<PathSpace*> PathSpaceAtomic::Decompose(){
  std::vector<PathSpace*> output;
  return output;
}

void PathSpaceAtomic::DrawGL(GUIState& state){
  uint ridx = input->robot_idx;
  Robot* robot = world->robots[ridx];
  const Config qi_in = input->q_init;
  const Config qg_in = input->q_goal;


  GLDraw::drawRobotAtConfig(robot, qi_in, lightGreen);
  GLDraw::drawRobotAtConfig(robot, qg_in, lightRed);

  if(vantage_path.size()>0){
    GLDraw::drawPath(vantage_path, magenta, 10);
  }
}

