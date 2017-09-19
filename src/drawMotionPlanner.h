#pragma once

#include <KrisLibrary/GLdraw/drawMesh.h>
#include <KrisLibrary/robotics/IK.h>
#include <KrisLibrary/robotics/IKFunctions.h>
#include <KrisLibrary/camera/viewport.h>
#include <KrisLibrary/math3d/rotation.h>
#include <View/ViewRobot.h>
#include <View/ViewIK.h>

#include "planner/planner.h"
#include "elements/wrench_field.h"
#include "elements/simplicial_complex.h"

namespace GLDraw {
  void drawRobotExtras(ViewRobot *robot, GLColor bodyColor=GLColor(0.5,0.5,0.5), double COMradius=0.05);
  void drawIKextras(ViewRobot *viewrobot, Robot *robot, std::vector<IKGoal> &constraints, std::vector<int> linksInCollision, GLColor selectedLinkColor);
  void drawUniformForceField();
  void drawForceField(WrenchField &wrenchfield);
  void drawWrenchField(WrenchField &wrenchfield);
  void drawCylinderArrowAtPosition(Vector3 &pos, Vector3 &dir, GLColor &color);

  void drawGLPathKeyframes(Robot *robot, std::vector<uint> keyframe_indices, std::vector<std::vector<Matrix4> > mats, vector<GLDraw::GeometryAppearance> appearanceStack, GLColor color = GLColor(0.8,0.8,0.8,1.0), double scale = 1.0);

  void drawShortestPath( SimplicialComplex& cmplx );
  void drawSimplicialComplex( SimplicialComplex& cmplx );
  void drawSwathVolume(Robot *robot, std::vector<std::vector<Matrix4> > mats, vector<GLDraw::GeometryAppearance> appearanceStack, GLColor swathVolumeColor=GLColor(0.5,0.8,0.5,0.5));

  void drawGLPathSweptVolume(Robot *robot, std::vector<std::vector<Matrix4> > mats, vector<GLDraw::GeometryAppearance> appearanceStack, GLColor sweptvolumeColor = GLColor(0.7,0.0,0.9,0.2), double sweptvolumeScale = 0.98);
  //void drawGLPathMilestones(Robot *robot, std::vector<Config> &keyframes, uint Nmilestones);
  void drawGLPathStartGoal(Robot *robot, const Config &p_init, const Config &p_goal);

  void drawRobotAtConfig(Robot *robot, const Config &q, GLColor color=GLColor(1,0,0), double scale = 1.0);
  void drawPlannerTree(const SerializedTree &_stree, GLColor colorTree=GLColor(0.3,0.7,0.3));
  void drawAxesLabels(Camera::Viewport& viewport);
  void drawFrames(std::vector< std::vector<Vector3> > &frames, std::vector<double> frameLength);

  void drawCenterOfMassPathFromController(WorldSimulation &sim);
  void drawForceEllipsoid( const ODERobot *robot );
  void drawDistanceRobotTerrain(const ODERobot *robot, const Terrain* terrain);

  void drawWireEllipsoid(Vector3 &c, Vector3 &u, Vector3 &v, Vector3 &w, int numSteps=16);
  void drawEllipsoid(Vector3 &c, Vector3 &u, Vector3 &v, Vector3 &w, int numSteps=16);

  void drawWorkspaceApproximationSpheres(std::vector<PlannerOutput> &pout);

  void drawPath( const std::vector<Config> &path, GLColor &c, double linewidth = 10);

  void setColor(GLColor &c);
};
