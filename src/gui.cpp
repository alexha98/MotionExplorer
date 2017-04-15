#include <tinyxml.h>
#include <iostream>
#include <fstream>
#include "gui.h"
#include "drawMotionPlanner.h"
#include "util.h"


const GLColor bodyColor(0.1,0.1,0.1);
const GLColor selectedLinkColor(1.0,1.0,0.5);
const double sweptvolumeScale = 0.98;
const double sweptVolume_q_spacing = 0.01;

ForceFieldBackend::ForceFieldBackend(RobotWorld *world)
    : SimTestBackend(world)
{
  world->background = GLColor(1,1,1);

  if(world->robots.size()>1){
    std::cout << "[ERROR]" << std::endl;
    std::cout << "there are " << world->robots.size() << " robots in this world. We only support 1." << std::endl;
    exit(0);
  }

  drawForceField = 0;
  drawIKextras = 0;

  drawPlannerTree = 1;
  drawPlannerStartGoal = 1;
  drawRigidObjects = 1;
  drawRigidObjectsEdges = 1;
  drawRigidObjectsFaces = 0;

  drawAxes = 0;
  drawAxesLabels = 0;
  drawRobot = 0;
  drawRobotExtras = 0;

  MapButtonToggle("draw_planner_tree",&drawPlannerTree);
  MapButtonToggle("draw_planner_start_goal",&drawPlannerStartGoal);

  MapButtonToggle("draw_rigid_objects_faces",&drawRigidObjectsFaces);
  MapButtonToggle("draw_rigid_objects_edges",&drawRigidObjectsEdges);

  MapButtonToggle("draw_robot_extras",&drawRobotExtras);
  MapButtonToggle("draw_robot",&drawRobot);
  MapButtonToggle("draw_fancy_coordinate_axes",&drawAxes);
  MapButtonToggle("draw_fancy_coordinate_axes_labels",&drawAxesLabels);

  drawPathSweptVolume.clear();
  drawPathMilestones.clear();
  drawPathStartGoal.clear();


  _frames.clear();
  swept_volume_paths.clear();
}


void ForceFieldBackend::Start()
{
  BaseT::Start();


  //disable higher drawing functions
  //drawBBs,drawPoser,drawDesired,drawEstimated,drawContacts,drawWrenches,drawExpanded,drawTime,doLogging
  drawPoser = 0;

  //settings["desired"]["color"][0] = 1;
  //settings["desired"]["color"][1] = 0;
  //settings["desired"]["color"][2] = 0;
  //settings["desired"]["color"][3] = 0.5;
  //camera.dist = 4;
  //viewport.n = 0.1;
  //viewport.f = 100;
  //viewport.setLensAngle(DtoR(90.0));
  //DisplayCameraTarget();
  //Camera::CameraController_Orbit camera;
  //Camera::Viewport viewport;
  show_frames_per_second = true;

  Robot *robot = world->robots[0];
  _appearanceStack.clear();
  _appearanceStack.resize(robot->links.size());

  for(size_t i=0;i<robot->links.size();i++) {
    GLDraw::GeometryAppearance& a = *robot->geomManagers[i].Appearance();
    _appearanceStack[i]=a;
  }

  drawPathSweptVolume.clear();
  drawPathMilestones.clear();
  drawPathStartGoal.clear();

  std::cout << "Setting swept volume paths" << std::endl;
  for(int i = 0; i < getNumberOfPaths(); i++){
    drawPathSweptVolume.push_back(0);
    drawPathMilestones.push_back(0);
    drawPathStartGoal.push_back(1);
  }
  for(int i = 0; i < getNumberOfPaths(); i++){
    std::string dpsv = "draw_path_swept_volume_"+std::to_string(i);
    MapButtonToggle(dpsv.c_str(),&drawPathSweptVolume.at(i));
    std::string dpms = "draw_path_milestones"+std::to_string(i);
    MapButtonToggle(dpms.c_str(),&drawPathMilestones.at(i));
    std::string dpsg = "draw_path_start_goal"+std::to_string(i);
    MapButtonToggle(dpsg.c_str(),&drawPathStartGoal.at(i));
  }

}

//############################################################################
//############################################################################


void ForceFieldBackend::RenderWorld()
{
  DEBUG_GL_ERRORS()
  drawDesired=0;

  for(size_t i=0;i<world->terrains.size();i++)
    world->terrains[i]->DrawGL();

  if(drawRigidObjects){
    for(size_t i=0;i<world->rigidObjects.size();i++){
      //
      //visualization of vertices/faces is implemented in 
      //Krislibrary/GLDraw/GeometryAppearances.h
      //but edges are not yet!
      //

      RigidObject *obj = world->rigidObjects[i];
      GLDraw::GeometryAppearance* a = obj->geometry.Appearance();
      a->SetColor(GLColor(0.8,0.8,0.8));
      a->drawFaces = false;
      a->drawEdges = false;
      a->drawVertices = false;
      if(drawRigidObjectsFaces) a->drawFaces = true;
      if(drawRigidObjectsEdges) a->drawEdges = true;
      //a->vertexSize = 10;
      a->edgeSize = 20;
      obj->DrawGL();
    }
  }
  //WorldGUIBackend::RenderWorld();

  if(drawRobot){
    for(size_t i=0;i<world->robots.size();i++) {
      for(size_t j=0;j<world->robots[i]->links.size();j++) {
        sim.odesim.robot(i)->GetLinkTransform(j,world->robots[i]->links[j].T_World);
        world->robotViews[i].DrawLink_World(j);
      }
    }
  }


  BaseT::RenderWorld();

  glEnable(GL_BLEND);
  allWidgets.Enable(&allRobotWidgets,drawPoser==1);
  allWidgets.DrawGL(viewport);
  vector<ViewRobot> viewRobots = world->robotViews;

  Robot *robot = world->robots[0];
  ViewRobot *viewRobot = &viewRobots[0];

  //############################################################################
  // Visualize
  //
  // drawrobotextras      : COM, skeleton
  // drawikextras         : contact links, contact directions
  // drawforcefield       : a flow/force field on R^3
  // drawpathsweptvolume  : swept volume along path
  // drawplannerstartgoal : start/goal configuration of motion planner
  // drawplannertree      : Cspace tree visualized as COM tree in W
  // drawaxes             : fancy coordinate axes
  // drawaxeslabels       : labelling of the coordinate axes [needs fixing]
  //############################################################################

  if(drawRobotExtras) GLDraw::drawRobotExtras(viewRobot);
  if(drawIKextras) GLDraw::drawIKextras(viewRobot, robot, _constraints, _linksInCollision, selectedLinkColor);
  if(drawForceField) GLDraw::drawUniformForceField();
  if(drawPlannerStartGoal) GLDraw::drawGLPathStartGoal(robot, planner_p_init, planner_p_goal);

  for(int i = 0; i < swept_volume_paths.size(); i++){
    SweptVolume sv = swept_volume_paths.at(i);

    if(drawPathSweptVolume.at(i)) GLDraw::drawGLPathSweptVolume(robot, sv.GetMatrices(), _appearanceStack,sv.GetColor());
    if(drawPathMilestones.at(i)) GLDraw::drawGLPathKeyframes(robot, sv.GetKeyframeIndices(), sv.GetMatrices(), _appearanceStack,sv.GetColorMilestones());
    if(drawPathStartGoal.at(i)) GLDraw::drawGLPathStartGoal(robot, sv.GetStart(), sv.GetGoal());
  }

  if(drawPlannerTree) GLDraw::drawPlannerTree(_stree);
  if(drawAxes) drawCoordWidget(1); //void drawCoordWidget(float len,float axisWidth=0.05,float arrowLen=0.2,float arrowWidth=0.1);
  if(drawAxesLabels) GLDraw::drawAxesLabels(viewport);
  if(drawFrames) GLDraw::drawFrames(_frames, _frameLength);

  

}//RenderWorld

void ForceFieldBackend::VisualizeStartGoal(const Config &p_init, const Config &p_goal)
{
  drawPlannerStartGoal = 1;
  planner_p_init = p_init;
  planner_p_goal = p_goal;
}

bool ForceFieldBackend::Load(TiXmlElement *node)
{

  _stree.clear();
  _keyframes.clear();
  planner_p_goal.setZero();
  planner_p_init.setZero();

  if(0!=strcmp(node->Value(),"GUI")) {
    std::cout << "Not a GUI file" << std::endl;
    return false;
  }
  TiXmlElement* e=node->FirstChildElement();
  while(e != NULL) 
  {
    if(0==strcmp(e->Value(),"robot")) {
      TiXmlElement* c=e->FirstChildElement();
      while(c!=NULL)
      {
        if(0==strcmp(c->Value(),"name")) {
          if(e->GetText()){
            stringstream ss(e->GetText());
            ss >> _robotname;
          }
        }
        if(0==strcmp(c->Value(),"configinit")) {
          stringstream ss(c->GetText());
          ss >> planner_p_init;
        }
        if(0==strcmp(c->Value(),"configgoal")) {
          stringstream ss(c->GetText());
          ss >> planner_p_goal;
        }
        c = c->NextSiblingElement();
      }
    }
    if(0==strcmp(e->Value(),"tree")) {
      TiXmlElement* c=e->FirstChildElement();
      while(c!=NULL)
      {
        if(0==strcmp(c->Value(),"node")) {
          SerializedTreeNode sn;
          sn.Load(c);
          _stree.push_back(sn);
        }
        c = c->NextSiblingElement();
      }
    }
    if(0==strcmp(e->Value(),"sweptvolume")) {

      TiXmlElement* c=e->FirstChildElement();
      std::vector<Config> keyframes;
      while(c!=NULL)
      {
        if(0==strcmp(c->Value(),"qitem")) {
          Config q;
          stringstream ss(c->GetText());
          ss >> q;
          _keyframes.push_back(q);
        }
        c = c->NextSiblingElement();
      }
      //AddPath(_keyframes);
    }
    e = e->NextSiblingElement();
  }

  return true;
}
bool ForceFieldBackend::Load(const char* file)
{
  std::string pdata = util::GetDataFolder();
  std::string in = pdata+"/gui/"+file;
  //std::string in = "../gui/state_2017_03_14.xml";

  std::cout << "loading data from "<<in << std::endl;

  TiXmlDocument doc(in.c_str());
  if(doc.LoadFile()){
    TiXmlElement *root = doc.RootElement();
    if(root){
      Load(root);
    }
  }else{
    std::cout << doc.ErrorDesc() << std::endl;
    std::cout << "ERROR" << std::endl;
  }
  //exit(0);
  return true;

}
bool ForceFieldBackend::Save(const char* file)
{
  std::string out;
  std::string pdata = util::GetDataFolder();

  if(!file){
    std::string date = util::GetCurrentTimeString();
    out = pdata+"/gui/state_"+date+".xml";
  }else{
    out = pdata+"/gui/"+file;
  }

  std::cout << "saving data to "<< out << std::endl;

  TiXmlDocument doc;
  TiXmlDeclaration * decl = new TiXmlDeclaration( "1.0", "", "" );
  doc.LinkEndChild(decl);
  TiXmlElement *node = new TiXmlElement("GUI");
  Save(node);

  doc.LinkEndChild(node);
  doc.SaveFile(out.c_str());

  return true;

}
bool ForceFieldBackend::Save(TiXmlElement *node)
{
  node->SetValue("GUI");

  //###################################################################
  {
    TiXmlElement c("robot");

    {
      TiXmlElement cc("name");
      stringstream ss;
      ss<<_robotname;
      TiXmlText text(ss.str().c_str());
      cc.InsertEndChild(text);
      c.InsertEndChild(cc);
    }
    {
      TiXmlElement cc("configinit");
      stringstream ss;
      ss<<planner_p_init<<endl;
      TiXmlText text(ss.str().c_str());
      cc.InsertEndChild(text);
      c.InsertEndChild(cc);
    }
    {
      TiXmlElement cc("configgoal");
      stringstream ss;
      ss<<planner_p_goal;
      TiXmlText text(ss.str().c_str());
      cc.InsertEndChild(text);
      c.InsertEndChild(cc);
    }
    node->InsertEndChild(c);
  }
  //###################################################################
  {
    TiXmlElement c("tree");
    for(int i = 0; i < _stree.size(); i++){
      TiXmlElement cc("node");
      _stree.at(i).Save(&cc);
      c.InsertEndChild(cc);
    }

    node->InsertEndChild(c);
  }
  //###################################################################
  {
    TiXmlElement c("sweptvolume");
    for(int i = 0; i < _keyframes.size(); i++){
      TiXmlElement cc("qitem");
      stringstream ss;
      ss<<_keyframes.at(i);
      TiXmlText text(ss.str().c_str());
      cc.InsertEndChild(text);
      c.InsertEndChild(cc);
    }
    node->InsertEndChild(c);
  }

  return true;

}

//############################################################################
//############################################################################

#include <KrisLibrary/graph/Tree.h>

void ForceFieldBackend::VisualizePlannerTree(const SerializedTree &tree)
{
  _stree = tree;
  drawPlannerTree=1;
}

std::vector<Config> ForceFieldBackend::getKeyFrames()
{
  return _keyframes;
}
uint ForceFieldBackend::getNumberOfPaths(){
  return this->swept_volume_paths.size();
}

void ForceFieldBackend::VisualizeFrame( const Vector3 &p, const Vector3 &e1, const Vector3 &e2, const Vector3 &e3, double frameLength)
{
  vector<Vector3> frame;
  frame.push_back(p);
  frame.push_back(e1);
  frame.push_back(e2);
  frame.push_back(e3);

  _frames.push_back(frame);
  _frameLength.push_back(frameLength);
}

void ForceFieldBackend::AddPath(const std::vector<Config> &keyframes, GLColor color, uint Nkeyframes_alongpath)
{
  Robot *robot = world->robots[0];
  SweptVolume sv(robot, keyframes, Nkeyframes_alongpath);
  sv.SetColor(color);
  swept_volume_paths.push_back(sv);
}

//############################################################################
//############################################################################

void ForceFieldBackend::SetIKConstraints( vector<IKGoal> constraints, string robotname){
  _constraints = constraints;
  _robotname = robotname;
  drawIKextras = 1;
}
void ForceFieldBackend::SetIKCollisions( vector<int> linksInCollision )
{
  _linksInCollision = linksInCollision;
  drawIKextras = 1;
}
bool ForceFieldBackend::OnCommand(const string& cmd,const string& args){
  stringstream ss(args);
  std::cout << "OnCommand: " << cmd << std::endl;

  if(cmd=="advance") {
    //static uint i = 0;

    //if(i>_keyframes.size()-1){
    //  return BaseT::OnCommand(cmd,args);
    //}

    //Config q = _keyframes.at(i);
    //Config q2 = _keyframes.at(i+1);
    //double dt = 1.0/_keyframes.size();
    //Config dq = (q-q2)/dt;
    ////if(i==0) dq.setZero();
    //stringstream qstr;
    //qstr<<q;

    ////string cmd( (i<=0)?("set_qv"):("append_qv") );
    //string cmd( (i<=0)?("set_q"):("append_q") );
    //sim.robotControllers[0]->SendCommand(cmd,qstr.str());
    //i++;

    std::cout << sim.time << std::endl;
    std::vector<Real> times;
    for(int i = 0; i < _keyframes.size(); i++){
      times.push_back(i);
    }
    SendLinearPath(times,_keyframes);

  }else if(cmd=="draw_rigid_objects_faces_toggle") {
    toggle(drawRigidObjectsFaces);
  }else if(cmd=="draw_rigid_objects_edges_toggle") {
    toggle(drawRigidObjectsEdges);
  }else if(cmd=="load_motion_planner") {
    std::cout << "loading file " << args.c_str() << std::endl;
  }else if(cmd=="save_motion_planner") {
    std::cout << "saving file " << args.c_str() << std::endl;
  }else{
    return BaseT::OnCommand(cmd,args);
  }
  SendRefresh();
  return true;
}
//############################################################################
//############################################################################

GLUIForceFieldGUI::GLUIForceFieldGUI(GenericBackendBase* _backend,RobotWorld* _world)
    :BaseT(_backend,_world)
{
}
void GLUIForceFieldGUI::browser_control(int control) {
  glutPostRedisplay();
  if(control==1){
    //browser
    std::string file_name = browser->get_file();
    //file = fopen(file_name.c_str(),"r"); 
    std::cout << "opening file " << file_name << std::endl;
  }
}
bool GLUIForceFieldGUI::Initialize()
{
  if(!BaseT::Initialize()) return false;
  

  ForceFieldBackend* _backend = static_cast<ForceFieldBackend*>(backend);

  panel = glui->add_rollout("Motion Planning");
  checkbox = glui->add_checkbox_to_panel(panel, "Draw Object Edges");
  AddControl(checkbox,"draw_rigid_objects_edges");
  checkbox->set_int_val(_backend->drawRigidObjectsEdges);

  checkbox = glui->add_checkbox_to_panel(panel, "Draw Object Faces");
  AddControl(checkbox,"draw_rigid_objects_faces");
  checkbox->set_int_val(_backend->drawRigidObjectsFaces);

  checkbox = glui->add_checkbox_to_panel(panel, "Draw Planner Start Goal");
  AddControl(checkbox,"draw_planner_start_goal");
  checkbox->set_int_val(_backend->drawPlannerStartGoal);

  checkbox = glui->add_checkbox_to_panel(panel, "Draw Planning Tree");
  AddControl(checkbox,"draw_planner_tree");
  checkbox->set_int_val(_backend->drawPlannerTree);

  uint N = _backend->getNumberOfPaths();

  std::cout << "N paths " << N << std::endl;
  for(int i = 0; i < N; i++){
    std::string prefix = "Path "+std::to_string(i)+" :";

    std::string dpsv = "draw_path_swept_volume_"+std::to_string(i);
    std::string descr1 = prefix + "Draw Swept Volume";
    checkbox = glui->add_checkbox_to_panel(panel, descr1.c_str());
    AddControl(checkbox,dpsv.c_str());
    checkbox->set_int_val(_backend->drawPathSweptVolume.at(i));

    std::string dpms = "draw_path_milestones"+std::to_string(i);
    std::string descr2 = prefix + "Draw Milestones";
    checkbox = glui->add_checkbox_to_panel(panel, descr2.c_str());
    AddControl(checkbox,dpms.c_str());
    checkbox->set_int_val(_backend->drawPathMilestones.at(i));

    std::string dpsg = "draw_path_start_goal"+std::to_string(i);
    std::string descr3 = prefix + "Draw Start Goal";
    checkbox = glui->add_checkbox_to_panel(panel, descr3.c_str());
    AddControl(checkbox,dpsg.c_str());
    checkbox->set_int_val(_backend->drawPathStartGoal.at(i));
  }

    //AddControl(glui->add_button_to_panel(panel,"Save state"),"save_state");
  GLUI_Button* button;
  button = glui->add_button_to_panel(panel,">> Save state");
  AddControl(button, "save_motion_planner");
  button = glui->add_button_to_panel(panel,">> Load state");
  AddControl(button, "load_motion_planner");

  glui->add_button_to_panel(panel,"Quit",0,(GLUI_Update_CB)exit);
  //browser = new GLUI_FileBrowser(panel, "Loading New State", false, 1, 
      //static_cast<void(GLUIForceFieldGUI::*)(int)>(&GLUIForceFieldGUI::browser_control));
  //browser->set_h(100);
  //browser->set_w(100);
  //browser->set_allow_change_dir(0);
  //browser->fbreaddir("../data/gui");

  panel = glui->add_rollout("Fancy Decorations");
  checkbox = glui->add_checkbox_to_panel(panel, "Draw Coordinate Axes");
  AddControl(checkbox,"draw_fancy_coordinate_axes");
  checkbox->set_int_val(_backend->drawAxes);

  checkbox = glui->add_checkbox_to_panel(panel, "Draw Coordinate Axes Labels[TODO]");
  AddControl(checkbox,"draw_fancy_coordinate_axes_labels");
  checkbox->set_int_val(_backend->drawAxesLabels);

  checkbox = glui->add_checkbox_to_panel(panel, "Draw Robot");
  AddControl(checkbox,"draw_robot");
  checkbox->set_int_val(_backend->drawRobot);

  checkbox = glui->add_checkbox_to_panel(panel, "Draw Robot COM+Skeleton");
  AddControl(checkbox,"draw_robot_extras");
  checkbox->set_int_val(_backend->drawRobotExtras);

  UpdateGUI();

//################################################################################
  //KEYMAPS
  //add keymaps to GUI (TODO: vimerize and make it automatically appear in -h)
  //Handle_Keypress: show keymap on -h
  //Backend::OnCommand: handle the action
//################################################################################

  AddToKeymap("r","reset");
  AddToKeymap("f","draw_rigid_objects_faces_toggle");
  AddToKeymap("e","draw_rigid_objects_edges_toggle");
  AddToKeymap("s","toggle_simulate");

  return true;

}
void GLUIForceFieldGUI::AddToKeymap(const char *key, const char *s){
  AnyCollection c;
  std::string type =  "{type:key_down,key:"+std::string(key)+"}";
  bool res=c.read(type.c_str());
  Assert(res == true);
  AddCommandRule(c,s,"");

  std::string descr(s);
  descr = std::regex_replace(descr, std::regex("_"), " ");

  //_keymap[key] = descr.c_str();
  _keymap[key] = descr;
}

void GLUIForceFieldGUI::Handle_Keypress(unsigned char c,int x,int y)
{
    switch(c) {
      case 'h':
        BaseT::Handle_Keypress(c,x,y);

        std::cout << "--- swept volume" << std::endl;
        for (Keymap::iterator it=_keymap.begin(); it!=_keymap.end(); ++it){
          std::cout << it->first << ": " << it->second << '\n';
        }

        //printf("save motion planner \n");
        break;
      default:
        BaseT::Handle_Keypress(c,x,y);
    }
}




