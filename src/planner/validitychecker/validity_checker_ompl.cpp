#include "planner/validitychecker/validity_checker_ompl.h"

OMPLValidityChecker::OMPLValidityChecker(const ob::SpaceInformationPtr &si, CSpaceOMPL *ompl_space_, CSpace *inner_):
  ob::StateValidityChecker(si), ompl_space(ompl_space_), inner(inner_)
{
}

bool OMPLValidityChecker::isValid(const ob::State* state) const
{
  const ob::StateSpacePtr ssp = si_->getStateSpace();
  Config q = ompl_space->OMPLStateToConfig(state);
  SingleRobotCSpace* csi = static_cast<SingleRobotCSpace*>(inner);
  return isCollisionFree(csi, q);
}

//same as Singlerobotcspace but ignore other robots
bool OMPLValidityChecker::isCollisionFree(SingleRobotCSpace *space, Config q) const{
  Robot* robot = space->GetRobot();
  robot->UpdateConfig(q);
  robot->UpdateGeometry();

  int id = space->world.RobotID(space->index);
  vector<int> idrobot(1,id);
  vector<int> idothers;
  for(size_t i=0;i<space->world.terrains.size();i++)
    idothers.push_back(space->world.TerrainID(i));
  for(size_t i=0;i<space->world.rigidObjects.size();i++)
    idothers.push_back(space->world.RigidObjectID(i));

  pair<int,int> res;

  //selfcollision checking
  res = space->settings->CheckCollision(space->world,idrobot);
  if(res.first >= 0) return false;
  //environment collision checking
  res = space->settings->CheckCollision(space->world,idrobot,idothers);
  if(res.first >= 0) return false;

  return true;
}
bool OMPLValidityChecker::isNecessary(const ob::State* state) const
{
  return isValid(state);
}
bool OMPLValidityChecker::isSufficient(const ob::State* state) const
{
  exit(0);
  return false;
}


OMPLValidityCheckerInnerOuter::OMPLValidityCheckerInnerOuter(const ob::SpaceInformationPtr &si, CSpaceOMPL *ompl_space_, CSpace *inner_, CSpace *outer_):
  OMPLValidityChecker(si, ompl_space_, inner_), outer(outer_)
{
}
bool OMPLValidityCheckerInnerOuter::isValid(const ob::State* state) const
{
  const ob::StateSpacePtr ssp = si_->getStateSpace();
  Config q = ompl_space->OMPLStateToConfig(state);
  SingleRobotCSpace* csi = static_cast<SingleRobotCSpace*>(inner);
  SingleRobotCSpace* cso = static_cast<SingleRobotCSpace*>(outer);
  return isCollisionFree(csi, q) && (!isCollisionFree(cso,q));
}

OMPLValidityCheckerNecessarySufficient::OMPLValidityCheckerNecessarySufficient(const ob::SpaceInformationPtr &si, CSpaceOMPL *ompl_space_, CSpace *outer_):
  OMPLValidityChecker(si, ompl_space_, ompl_space_->GetCSpacePtr()), outer(outer_)
{
}
bool OMPLValidityCheckerNecessarySufficient::isSufficient(const ob::State* state) const
{
  Config q = ompl_space->OMPLStateToConfig(state);
  SingleRobotCSpace* cso = static_cast<SingleRobotCSpace*>(outer);
  return isCollisionFree(cso, q);
}
