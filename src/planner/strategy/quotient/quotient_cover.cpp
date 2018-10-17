#include "common.h"
#include "quotient_cover.h"
#include "elements/plannerdata_vertex_annotated.h"
#include "planner/cspace/validitychecker/validity_checker_ompl.h"
#include <limits>
#include <boost/graph/astar_search.hpp>
#include <boost/graph/copy.hpp>
#include <boost/graph/incremental_components.hpp>
#include <boost/property_map/vector_property_map.hpp>
#include <boost/property_map/transform_value_property_map.hpp>
#include <boost/foreach.hpp>
#include <boost/graph/graphviz.hpp>

#define foreach BOOST_FOREACH

using namespace ompl::geometric;

QuotientCover::QuotientCover(const base::SpaceInformationPtr &si, Quotient *parent ): BaseT(si, parent)
{
  setName("QuotientCover"+std::to_string(id));
}

QuotientCover::~QuotientCover(void)
{
}
//#############################################################################
//SETUP
//#############################################################################

void QuotientCover::setup(void)
{
  BaseT::setup();
  if (!nearest_cover){
    nearest_cover.reset(tools::SelfConfig::getDefaultNearestNeighbors<Configuration *>(this));
    nearest_cover->setDistanceFunction([this](const Configuration *a, const Configuration *b)
                             {
                                return DistanceNeighborhoodNeighborhood(a,b);
                              });
  }
  if (!nearest_vertex){
    nearest_vertex.reset(tools::SelfConfig::getDefaultNearestNeighbors<Configuration *>(this));
    nearest_vertex->setDistanceFunction([this](const Configuration *a, const Configuration *b)
                             {
                                return DistanceConfigurationConfiguration(a,b);
                              });
  }

  const double maxBias = 0.9;
  if(goalBias+voronoiBias >= maxBias)
  {
    std::cout << "goal and voronoi bias are too big." << std::endl;
    std::cout << "goal bias: " << goalBias << std::endl;
    std::cout << "voronoi bias: " << voronoiBias << std::endl;
    std::cout << "(should be below "<< maxBias << ")" << std::endl;
    exit(0);
  }
  graph[boost::graph_bundle].name = getName()+"_graph";
  graph[boost::graph_bundle].Q1 = Q1;

  saturated = false;
  isConnected = false;
  totalVolumeOfCover = 0.0;

  if (pdef_){
    //#########################################################################
    if (pdef_->hasOptimizationObjective()){
      opt_ = pdef_->getOptimizationObjective();
    }else{
      OMPL_ERROR("%s: Did not specify optimization function.", getName().c_str());
      exit(0);
    }
    //#########################################################################
    //Adding start configuration
    //#########################################################################
    if(const ob::State *state_start = pis_.nextStart()){
      q_start = new Configuration(Q1, state_start);
      if(!ComputeNeighborhood(q_start, true))
      {
        OMPL_ERROR("%s: Could not add start state!", getName().c_str());
        QuotientCover::Print(q_start, false);
        exit(0);
      }
    }else{
      OMPL_ERROR("%s: There are no valid initial states!", getName().c_str());
      exit(0);
    }

    //#########################################################################
    //Adding goal configuration
    //#########################################################################
    if(const ob::State *state_goal = pis_.nextGoal()){
      q_goal = new Configuration(Q1, state_goal);
      if(!ComputeNeighborhood(q_goal, true))
      {
        OMPL_ERROR("%s: Could not add goal state!", getName().c_str());
        exit(0);
      }
    }else{
      OMPL_ERROR("%s: There are no valid goal states!", getName().c_str());
      exit(0);
    }
    //#########################################################################
    //Parent data if available
    //#########################################################################
    if(parent != nullptr)
    {
      q_start->coset = static_cast<og::QuotientCover*>(parent)->q_start;
      q_goal->coset = static_cast<og::QuotientCover*>(parent)->q_goal;
    }
    q_start->isStart = true;
    q_goal->isGoal = true;
    v_start = AddConfigurationToCover(q_start);
    //#########################################################################
    //Check saturation
    //#########################################################################
    if(q_start->GetRadius() == std::numeric_limits<double>::infinity())
    {
      OMPL_INFORM("Note: start state covers quotient-space.");
      saturated = true;
    }
    //#########################################################################
    OMPL_INFORM("%s: ready with %lu states already in datastructure", getName().c_str(), nearest_cover->size());
    checkValidity();
  }else{
    setup_ = false;
  }

}
void QuotientCover::clear()
{
  BaseT::clear();
  totalVolumeOfCover = 0.0;

  //Nearestneighbors
  if(nearest_cover) nearest_cover->clear();
  if(nearest_vertex) nearest_vertex->clear();

  shortest_path_start_goal.clear();
  shortest_path_start_goal_necessary_vertices.clear();

  //Cover graph
  foreach (Vertex v, boost::vertices(graph)){
    if(graph[v]!=nullptr){
      if(!graph[v]->isStart && !graph[v]->isGoal){
        graph[v]->Remove(Q1);
      }
    }
  }
  graph.clear();

  //PDF
  pdf_necessary_configurations.clear();
  pdf_all_configurations.clear();

  //index maps
  vertexToIndexStdMap.clear();
  indexToVertexStdMap.clear();
  index_ctr = 0;
  isConnected = false;


  if(q_start){
    q_start->Clear();
    v_start = AddConfigurationToCover(q_start);

    if(q_start->GetRadius() == std::numeric_limits<double>::infinity())
    {
      OMPL_INFORM("Note: start state covers quotient-space.");
      saturated = true;
    }else{
      saturated = false;
    }
  }else{
    saturated = false;
  }
  if(q_goal){
    q_goal->Clear();
  }
}


//#############################################################################
//Configuration methods
//#############################################################################
void QuotientCover::AddConfigurationToPDF(Configuration *q)
{
  PDF_Element *q_element = pdf_all_configurations.add(q, q->GetImportance());
  q->SetPDFElement(q_element);

  if(!q->isSufficientFeasible){
    PDF_Element *q_necessary_element = pdf_necessary_configurations.add(q, q->GetRadius());
    q->SetNecessaryPDFElement(q_necessary_element);
  }
}

QuotientCover::Vertex QuotientCover::AddConfigurationToCoverWithoutAddingEdges(Configuration *q)
{
  //###########################################################################
  //safety checks
  //###########################################################################
  if(q->GetRadius()<minimum_neighborhood_radius)
  {
    OMPL_ERROR("Trying to add configuration to cover, but Neighborhood is too small.");
    std::cout << "state" << std::endl;
    Q1->printState(q->state);
    std::cout << "radius: " << q->GetRadius() << std::endl;
    exit(0);
  }
  totalVolumeOfCover += q->GetRadius();

  //###########################################################################
  //(1) add to cover graph
  //###########################################################################
  Vertex v = boost::add_vertex(q, graph);
  graph[v]->number_attempted_expansions = 0;
  graph[v]->number_successful_expansions = 0;

  //assign vertex to a unique index
  put(vertexToIndex, v, index_ctr);
  put(indexToVertex, index_ctr, v);
  graph[v]->index = index_ctr;
  index_ctr++;

  //###########################################################################
  //(2) add to nearest neighbor structure
  //###########################################################################
  nearest_cover->add(q);
  nearest_vertex->add(q);

  //###########################################################################
  //(3) add to PDF
  //###########################################################################
  AddConfigurationToPDF(q);

  if(verbose>0) std::cout << std::string(80, '-') << std::endl;
  if(verbose>0) std::cout << "*** ADD VERTEX " << q->index << " (radius " << q->GetRadius() << ")" << std::endl;
  if(verbose>0) Print(q);

  q->goal_distance = DistanceQ1(q, q_goal);

  if(q->parent_neighbor != nullptr){
    AddEdge(q, q->parent_neighbor);
  }

  return v;
}
QuotientCover::Vertex QuotientCover::AddConfigurationToCover(Configuration *q)
{
  std::vector<Configuration*> neighbors = GetConfigurationsInsideNeighborhood(q);

  Vertex v = AddConfigurationToCoverWithoutAddingEdges(q);

  //###########################################################################
  //Clean UP and Connect
  //(1) Remove All Configurations with neighborhoods inside current neighborhood
  //(2) Add Edges to All Configurations with centers inside the current neighborhood 
  if(verbose>0) std::cout << "Vertex: " << q->index << " has number of neighbors: " << neighbors.size() << std::endl;
  if(verbose>0) std::cout << std::string(80, '-') << std::endl;
  //for(uint k = 0; k < std::min(neighbors.size(),3LU); k++){
  for(uint k = 0; k < neighbors.size(); k++){
    Configuration *qn = neighbors.at(k);
    if(qn==q){
      std::cout << std::string(80, '#') << std::endl;
      std::cout << "configuration equals neighbor." << std::endl;
      std::cout << "state added with radius " << q->GetRadius() << std::endl;
      Q1->printState(q->state);
      std::cout << "state neighbor with radius " << qn->GetRadius() << std::endl;
      Q1->printState(qn->state);
      std::cout << std::string(80, '#') << std::endl;
      exit(0);
    }
    //do not connect to parent, we already connected that
    if(qn == q->parent_neighbor) continue;
    if(IsNeighborhoodInsideNeighborhood(qn, q))
    {
      //do not delete start/goal
      if(qn->isStart){
        AddEdge(q, qn);
      }else if(qn->isGoal){
        AddEdge(q, qn);
      }else{
        //old constellation
        //v1 -------                                    |
        //          \                                   |
        //v2-------- vn ------- vq                      |
        //          /                                   |
        //v3--------                                    |
        //
        //after edge adding 
        //v1 -------------------                        |
        //          \           \                       |
        //v2-------- vn ------- vq                      |
        //          /           /                       |
        //v3--------------------                        |
        //after edge removal 
        //v1 -------------------                        |
        //                      \                       |
        //v2------   vn   ----- vq                      |
        //        \------/      /                       |
        //v3--------------------                        |
        //after vertex removal
        //v1 ---------------                            |
        //                  \                           |
        //v2--------------- vq                          |
        //                  /                           |
        //v3----------------                            |
        //
        //get all edges into qn, and rewire them to q
        Vertex vn = get(indexToVertex, qn->index);
        OEIterator edge_iter, edge_iter_end, next; 
        boost::tie(edge_iter, edge_iter_end) = boost::out_edges(vn, graph);

        vertex_index_type vi = get(vertexToIndex, v);
        vertex_index_type vni = get(vertexToIndex, vn);
        if(verbose>1) std::cout << "vertex " << qn->index << " needs to be removed" << std::endl;
        if(verbose>1) std::cout << "vertex " << vi << "(" << q->index << ") and neighbor " << vni << "(" << qn->index << ")" << std::endl;
        if(verbose>1) std::cout << "removing " << boost::out_degree(vn, graph) << " edges from vertex " << vni << std::endl;

        for(next = edge_iter; edge_iter != edge_iter_end; edge_iter = next)
        {
          //add v_target to v
          ++next;
          const Vertex v_target = boost::target(*edge_iter, graph);
          //segfault, because v_target points to an already removed vertex!
          if(verbose>1) std::cout << "REWIRE EDGE from " << vni << "(" << get(vertexToIndex, vn) << ") to " << get(vertexToIndex, v_target) << "(" << graph[v_target]->index << ")" << std::endl;
          double d = DistanceQ1(graph[v_target], q);
          EdgeInternalState properties(d);

          if(v_target!=v)
          {
            //do not add to itself
            if(verbose>1) std::cout << "add " << get(vertexToIndex, v) << "-" << get(vertexToIndex, v_target) 
              << "(" << graph[v_target]->index << ")" << std::endl;

            //vertex might be invalidated
            boost::add_edge(v_target, v, properties, graph);
          }
        }
        boost::clear_vertex(vn, graph);
        RemoveConfigurationFromCover(qn);
      }
    }else{
      AddEdge(q, qn);
    }
  }
  return v;
}

void QuotientCover::AddEdge(Configuration *q_from, Configuration *q_to)
{
  double d = DistanceQ1(q_from, q_to);
  EdgeInternalState properties(d);
  Vertex v_from = get(indexToVertex, q_from->index);
  Vertex v_to = get(indexToVertex, q_to->index);
  boost::add_edge(v_from, v_to, properties, graph);
}

void QuotientCover::RemoveConfigurationFromCover(Configuration *q)
{
  totalVolumeOfCover -= q->GetRadius();

  //(1) Remove from PDF
  pdf_all_configurations.remove(static_cast<PDF_Element*>(q->GetPDFElement()));
  if(!q->isSufficientFeasible){
    pdf_necessary_configurations.remove(static_cast<PDF_Element*>(q->GetNecessaryPDFElement()));
  }
  //(2) Remove from nearest neighbors structure
  nearest_cover->remove(q);
  nearest_vertex->remove(q);

  if(verbose>0) std::cout << std::string(80, '-') << std::endl;
  if(verbose>0) std::cout << "*** REMOVE VERTEX " << q->index << std::endl;
  if(verbose>0) Q1->printState(q->state);

  //erase entry from indexmap
  Vertex vq = get(indexToVertex, q->index);
  vertexToIndexStdMap.erase(vq);
  indexToVertexStdMap.erase(q->index);

  //check that they do not exist in indextovertex anymore
  if(indexToVertexStdMap.count(q->index) > 0){
    vertex_index_type vit_before = get(vertexToIndex, vq);
    vertex_index_type vit_after = get(vertexToIndex, vq);
    std::cout << vit_before << " - " << vit_after << std::endl;
    exit(0);
  }

  boost::remove_vertex(vq, graph);

  //(3) Remove from cover graph
  q->Remove(Q1);
  delete q;
  q=nullptr;
}

bool QuotientCover::ComputeNeighborhood(Configuration *q, bool verbose)
{
  if(q==nullptr) return false;
  if(IsConfigurationInsideCover(q)){
    if(verbose) std::cout << "[ComputeNeighborhood] State Rejected: Inside Cover" << std::endl;
    q->Remove(Q1);
    q=nullptr;
    return false;
  }

  // QuotientCover::Print(q,false);
  // Q1->printState(q->state);
  bool feasible = Q1->isValid(q->state);

  if(feasible){

    if(IsOuterRobotFeasible(q->state))
    {
      q->isSufficientFeasible = true;
      q->SetOuterRadius(DistanceOuterRobotToObstacle(q->state));
    }

    q->SetRadius(DistanceInnerRobotToObstacle(q->state));
    if(q->GetRadius()<=minimum_neighborhood_radius){
      if(verbose) std::cout << "[ComputeNeighborhood] State Rejected: Radius Too Small (radius="<< q->GetRadius() << ")" << std::endl;
      q->Remove(Q1);
      q=nullptr;
      return false;
    }
  }else{
    if(verbose) std::cout << "[ComputeNeighborhood] State Rejected: Infeasible" << std::endl;
    q->Remove(Q1);
    q=nullptr;
    return false;
  }
  GetCosetFromQuotientSpace(q);

  return true;

}
void QuotientCover::RemoveConfigurationsFromCoverCoveredBy(Configuration *q)
{
  //If the neighborhood of q is a superset of any other neighborhood, then delete
  //the other neighborhood, and rewire the graph
  std::vector<Configuration*> neighbors = GetConfigurationsInsideNeighborhood(q);

  for(uint k = 0; k < neighbors.size(); k++){

    Configuration *qn = neighbors.at(k);

    //do not delete start/goal
    if(qn->isStart) continue;
    if(qn->isGoal) continue;

    if(IsNeighborhoodInsideNeighborhood(qn, q))
    {
      RemoveConfigurationFromCover(qn);
    }
  }
}


bool QuotientCover::IsConfigurationInsideNeighborhood(Configuration *q, Configuration *qn)
{
  return (DistanceConfigurationNeighborhood(q, qn) <= 0);
}

bool QuotientCover::IsConfigurationInsideCover(Configuration *q)
{
  std::vector<Configuration*> neighbors;
  nearest_cover->nearestK(q, 2, neighbors);
  if(neighbors.size()<=1) return false;
  return IsConfigurationInsideNeighborhood(q, neighbors.at(0)) && IsConfigurationInsideNeighborhood(q, neighbors.at(1));
}

double QuotientCover::GetImportance() const
{
  //approximation of total volume covered
  //double percentageCovered = totalVolumeOfCover/Q1->getStateSpace()->getMeasure();
  //the more sampled, the less important it becomes
  return 1.0/(totalVolumeOfCover+1);
}

void QuotientCover::Grow(double t)
{
  ob::PlannerTerminationCondition ptc( ob::timedPlannerTerminationCondition(t) );
  //#########################################################################
  //Do not grow the cover if it is saturated, i.e. it cannot be expanded anymore
  // --- we have successfully computed Q1_{free}, the free space of Q1
  //#########################################################################
  if(saturated) return;

  //#########################################################################
  //Sample a configuration different from the current cover
  //#########################################################################
  Configuration *q_random = SampleCoverBoundaryValid(ptc);
  if(q_random == nullptr) return;

  if(verbose>1) std::cout << "sampled "; Q1->printState(q_random->state);

  AddConfigurationToCover(q_random);
}
void QuotientCover::GetCosetFromQuotientSpace(Configuration *q)
{
  if(parent != nullptr)
  {
    //(1) project q onto Q0, obtain q_projected
    //(2) find nearest configuration to q_projected on Q0
    //(3) set nearest to coset of q

    og::QuotientCover *qcc_parent = static_cast<og::QuotientCover*>(parent);
    Configuration *q_projected = new Configuration(Q0);
    ExtractQ0Subspace(q->state, q_projected->state);
    q->coset = qcc_parent->Nearest(q_projected);
    q_projected->Remove(Q0);
  }
}


//#############################################################################
//Sampling Configurations (on quotient space)
//#############################################################################

void QuotientCover::SampleGoal(Configuration *q)
{
  q->state = Q1->cloneState(q_goal->state);
  q->coset = q_goal->coset;
}

void QuotientCover::SampleUniform(Configuration *q)
{
  if(parent == nullptr){
    Q1_sampler->sampleUniform(q->state);
  }else{
    ob::State *stateX1 = X1->allocState();
    ob::State *stateQ0 = Q0->allocState();
    X1_sampler->sampleUniform(stateX1);
    q->coset = static_cast<og::QuotientCover*>(parent)->SampleUniformQuotientCover(stateQ0);
    if(q->coset == nullptr){
      OMPL_ERROR("no coset found for state");
      Q1->printState(q->state);
      exit(0);
    }
    MergeStates(stateQ0, stateX1, q->state);
    X1->freeState(stateX1);
    Q0->freeState(stateQ0);
  }
}
void QuotientCover::SampleRandomNeighborhoodBoundary(Configuration *q)
{
  Configuration *q_nearest = pdf_all_configurations.sample(rng_.uniform01());
  q_nearest->number_attempted_expansions++;
  pdf_all_configurations.update(static_cast<PDF_Element*>(q_nearest->GetPDFElement()), q_nearest->GetImportance());

  SampleNeighborhoodBoundaryHalfBall(q, q_nearest);
  q->parent_neighbor = q_nearest;
  GetCosetFromQuotientSpace(q);
}

//#############################################################################
//Sampling Configurations (On boundary of cover)
//#############################################################################

QuotientCover::Configuration* QuotientCover::SampleCoverBoundary(std::string type)
{
  Configuration *q_random = new Configuration(Q1);
  if(type == "voronoi"){
    SampleUniform(q_random);
  }else if(type == "goal"){
    SampleGoal(q_random);
  }else if(type == "boundary"){
    SampleRandomNeighborhoodBoundary(q_random);
    return q_random;
  }else{
    std::cout << "sampling type " << type << " not recognized." << std::endl;
    exit(0);
  }

  Configuration *q_nearest = Nearest(q_random);
  Connect(q_nearest, q_random, q_random);
  q_random->parent_neighbor = q_nearest;
  if(q_random == q_nearest)
  {
    std::cout << "sampling invalid: nearest and random are equal" << std::endl;
    exit(0);
  }
  GetCosetFromQuotientSpace(q_random);
  return q_random;
}

//#############################################################################
// Main Sample Function
//#############################################################################

QuotientCover::Configuration* QuotientCover::Sample()
{
  return SampleCoverBoundary();
}
QuotientCover::Configuration* QuotientCover::SampleCoverBoundary()
{
  Configuration *q_random;
  if(!hasSolution){
    double r = rng_.uniform01();
    if(r<goalBias){
      q_random = SampleCoverBoundary("goal");
    }else{
      if(r < (goalBias + voronoiBias)){
        q_random = SampleCoverBoundary("voronoi");
      }else{
        q_random = SampleCoverBoundary("boundary");
      }
    }
  }else{
    std::cout << "hasSolution sampler NYI"<< std::endl;
    exit(0);
  }
  return q_random;
}

QuotientCover::Configuration* QuotientCover::SampleCoverBoundaryValid(ob::PlannerTerminationCondition &ptc)
{
  Configuration *q = nullptr;
  while(!ptc){
    q = SampleCoverBoundary();
    //ignore samples inside cover (rejection sampling)
    if(ComputeNeighborhood(q)) break;
    else q = nullptr;
  }
  return q;
}

QuotientCover::Configuration* QuotientCover::SampleUniformQuotientCover(ob::State *state) 
{
  Configuration *q_coset = pdf_all_configurations.sample(rng_.uniform01());

  Q1_sampler->sampleUniformNear(state, q_coset->state, q_coset->GetRadius());
  if(q_coset->isSufficientFeasible)
  {
    //project onto a random shell outside of sufficient neighborhood
    double r_necessary = q_coset->GetRadius();
    double r_sufficient = q_coset->openNeighborhoodOuterRadius;
    double r_01 = rng_.uniform01();
    double r_shell = r_sufficient + r_01*(r_necessary - r_sufficient);
    double d = Q1->distance(q_coset->state, state);
    Q1->getStateSpace()->interpolate(q_coset->state, state, r_shell/d, state);
  }
  return q_coset;
}

bool QuotientCover::SampleNeighborhoodBoundary(Configuration *q_random, const Configuration *q_center)
{
  //sample on boundary of open neighborhood
  // (1) first sample gaussian point qk around q
  // (2) project qk onto neighborhood
  // (*) this works because gaussian is symmetric around origin
  // (*) this does not work when qk is near to q, so we need to sample as long
  // as it is not near
  //
  double radius = q_center->GetRadius();
  double dist_q_qk = 0;

  if(minimum_neighborhood_radius >= radius){
    OMPL_ERROR("neighborhood is too small to sample the boundary.");
    std::cout << std::string(80, '#') << std::endl;
    std::cout << "Configuration " << std::endl;
    Print(q_center);
    std::cout << "radius: " << radius << std::endl;
    std::cout << "min distance: " << minimum_neighborhood_radius << std::endl;
    std::cout << std::string(80, '#') << std::endl;
    exit(0);
  }
  //sample as long as we are inside the ball of radius
  //minimum_neighborhood_radius
  while(dist_q_qk <= minimum_neighborhood_radius){
    Q1_sampler->sampleGaussian(q_random->state, q_center->state, 1);
    dist_q_qk = Q1->distance(q_center->state, q_random->state);
  }
  Q1->getStateSpace()->interpolate(q_center->state, q_random->state, radius/dist_q_qk, q_random->state);

  return true;
}
bool QuotientCover::SampleNeighborhoodBoundaryHalfBall(Configuration *q_random, const Configuration *q_center)
{
  SampleNeighborhoodBoundary(q_random, q_center);

  //q0 ---- q1 (q_center) ----- q2 (q_random)
  Configuration *q0 = q_center->parent_neighbor;

  //case1: no parent is available, there is no halfball
  if(q0==nullptr) return true;

  //case2: q_center is overlapping q0
  double radius_q0 = q0->GetRadius();
  double radius_q1 = q_center->GetRadius();
  if(2*radius_q0 < radius_q1) return true;

  //case3: q_random is lying already on outer half ball
  double d12 = DistanceConfigurationConfiguration(q_center, q_random);
  double d02 = DistanceConfigurationConfiguration(q0, q_random);
  if(d02 > d12) return true;

  //case4: q_random is lying on inner half ball => point reflection in q_center.
  //this is accomplished by interpolating from q_random to q_center, and then
  //following the same distance until the outer ball is hit
  Q1->getStateSpace()->interpolate(q_random->state, q_center->state, 2, q_random->state);

  return true;
}

//@brief: move from q_from to q_to until the neighborhood of q_from is
//intersected. Return the intersection point in q_out
void QuotientCover::Connect(const Configuration *q_from, const Configuration *q_to, Configuration *q_out)
{
  double radius = q_from->GetRadius();
  double dist_qfrom_qto = DistanceQ1(q_from, q_to);
  Q1->getStateSpace()->interpolate(q_from->state, q_to->state, radius/dist_qfrom_qto, q_out->state);
  //if(parent==nullptr){
  //  Q1->getStateSpace()->interpolate(q_from->state, q_to->state, step_size, q_interp->state);
  //}else{
  //  std::vector<const Configuration*> path = GetInterpolationPath(q_from, q_to);
  //  const Configuration *q_next = nullptr;
  //  double d = 0;
  //  double d_last_to_next = 0;
  //  uint ctr = 0;
  //  while(d < step_size && ctr < path.size()){
  //    d_last_to_next = DistanceQ1(path.at(ctr), path.at(ctr+1));
  //    d += d_last_to_next;
  //    q_next = path.at(ctr+1);
  //    ctr++;
  //  }
  //  const Configuration *q_last = path.at(ctr-1);
  //  double step = d_last_to_next - (d-step_size);
  //  Q1->getStateSpace()->interpolate(q_last->state, q_next->state, step/d_last_to_next, q_interp->state);
  //}

  // if(parent == nullptr)
  // {
  //   double dist_qfrom_qto = DistanceConfigurationConfiguration(q_from, q_to);
  //   Q1->getStateSpace()->interpolate(q_from->state, q_to->state, radius/dist_qfrom_qto, q_out->state);
  // }else{
  //   std::cout << "Connect() NYI" << std::endl;
  //   exit(0);

  // }

}

bool QuotientCover::IsNeighborhoodInsideNeighborhood(Configuration *lhs, Configuration *rhs)
{
  double distance_centers = DistanceQ1(lhs, rhs);
  double radius_rhs = rhs->GetRadius();
  double radius_lhs = lhs->GetRadius();
  if(radius_rhs < minimum_neighborhood_radius || radius_lhs < minimum_neighborhood_radius)
  {
    std::cout << "neighborhood inclusion failed" << std::endl;
    std::cout << "neighborhood1 with radius: " << radius_rhs << std::endl;
    Print(rhs);
    std::cout << "neighborhood2 with radius: " << radius_lhs << std::endl;
    Print(lhs);
    exit(0);
  }
  return (radius_rhs > (radius_lhs + distance_centers));
}

std::vector<QuotientCover::Configuration*> QuotientCover::GetConfigurationsInsideNeighborhood(Configuration *q)
{
  std::vector<Configuration*> neighbors;
  nearest_vertex->nearestR(q, q->GetRadius(), neighbors);
  return neighbors;
}


QuotientCover::Configuration* QuotientCover::Nearest(Configuration *q) const
{
  return nearest_cover->nearest(q);
}

//@TODO: should be fixed to reflect the distance on the underlying
//quotient-space, i.e. similar to QMPConnect. For now we use DistanceQ1

bool QuotientCover::Interpolate(const Configuration *q_from, Configuration *q_to)
{
  return Interpolate(q_from, q_to, q_to);
}

bool QuotientCover::Interpolate(const Configuration *q_from, const Configuration *q_to, Configuration *q_interp)
{
  double d = DistanceConfigurationConfiguration(q_from, q_to);
  double radius = q_from->GetRadius();
  return Interpolate(q_from, q_to, radius/d, q_interp);
}
bool QuotientCover::Interpolate(const Configuration *q_from, const Configuration *q_to, double step_size, Configuration *q_interp)
{
  if(parent==nullptr){
    Q1->getStateSpace()->interpolate(q_from->state, q_to->state, step_size, q_interp->state);
  }else{
    //move along path until step_size is reached. Then interpolate between the
    //waypoints to get the point on the path of distance step_size from q_from

    std::vector<const Configuration*> path = GetInterpolationPath(q_from, q_to);
    const Configuration *q_next = nullptr;
    double d = 0;
    double d_last_to_next = 0;
    uint ctr = 0;
    while(d < step_size && ctr < path.size()){
      d_last_to_next = DistanceQ1(path.at(ctr), path.at(ctr+1));
      d += d_last_to_next;
      q_next = path.at(ctr+1);
      ctr++;
    }
    const Configuration *q_last = path.at(ctr-1);
    double step = d_last_to_next - (d-step_size);
    Q1->getStateSpace()->interpolate(q_last->state, q_next->state, step/d_last_to_next, q_interp->state);
  }
  return true;
}


//#############################################################################
//Distance Functions
//#############################################################################

double QuotientCover::DistanceQ1(const Configuration *q_from, const Configuration *q_to)
{
  return Q1->distance(q_from->state, q_to->state);
}

double QuotientCover::DistanceX1(const Configuration *q_from, const Configuration *q_to)
{
  ob::State *stateFrom = X1->allocState();
  ob::State *stateTo = X1->allocState();
  ExtractX1Subspace(q_from->state, stateFrom);
  ExtractX1Subspace(q_to->state, stateTo);
  double d = X1->distance(stateFrom, stateTo);
  X1->freeState(stateFrom);
  X1->freeState(stateTo);
  return d;
}

double QuotientCover::DistanceConfigurationConfiguration(const Configuration *q_from, const Configuration *q_to)
{
  if(parent == nullptr){
    //the very first quotient-space => usual metric
    return DistanceQ1(q_from, q_to);
  }else{
    if(q_to->coset == nullptr || q_from->coset == nullptr)
    {
      //std::cout << std::string(80, '#') << std::endl;
      //std::cout << "[ERROR] could not find coset for a configuration" << std::endl;
      //std::cout << std::string(80, '#') << std::endl;
      //std::cout << "from:"; Print(q_from, false);
      //std::cout << std::string(80, '-') << std::endl;
      //std::cout << "to:"; Print(q_to);
      //std::cout << std::string(80, '-') << std::endl;
      //std::cout << *this << std::endl;
      //exit(0);
      //could not find a coset on the quotient-space  => usual metric
      //fall-back metric
      return DistanceQ1(q_from, q_to);
    }
    return DistanceConfigurationConfigurationCover(q_from, q_to)+DistanceX1(q_from, q_to);
  }
}


double QuotientCover::DistanceConfigurationConfigurationCover(const Configuration *q_from, const Configuration *q_to)
{
  std::vector<const Configuration*> path = GetInterpolationPath(q_from, q_to);
  double d = 0;
  for(uint k = 0; k < path.size()-1; k++){
    d += DistanceQ1(path.at(k), path.at(k+1));
  }
  return d;
}

std::vector<const QuotientCover::Configuration*> QuotientCover::GetInterpolationPath(const Configuration *q_from, const Configuration *q_to)
{
  if(q_from->coset == nullptr || q_to->coset == nullptr){
    OMPL_ERROR("Cannot interpolate without cosets");
    std::cout << std::string(80, '#') << std::endl;
    std::cout << "[ERROR] could not find coset for a configuration" << std::endl;
    std::cout << std::string(80, '#') << std::endl;
    std::cout << "from:"; Print(q_from, false);
    std::cout << std::string(80, '-') << std::endl;
    std::cout << "to:"; Print(q_to);
    std::cout << std::string(80, '-') << std::endl;
    std::cout << *this << std::endl;
    exit(0);
  }
  std::vector<const Configuration*> path_Q1;

  og::QuotientCover *parent_chart = dynamic_cast<og::QuotientCover*>(parent);

  //(0) get shortest path on Q0 between q_from->coset to q_to->coset
  std::vector<const Configuration*> path_Q0_cover = parent_chart->GetCoverPath(q_from->coset, q_to->coset);

  if(path_Q0_cover.empty()){
    //(1) cosets are equivalent => straight line interpolation
    path_Q1.push_back(q_from);
    path_Q1.push_back(q_to);
  }else{
    path_Q0_cover.erase(path_Q0_cover.begin());
    path_Q0_cover.pop_back();

    if(path_Q0_cover.empty()){
      //(2) cosets are neighbors => straight line interpolation
      path_Q1.push_back(q_from);
      path_Q1.push_back(q_to);
    }else{
      //(3) cosets have nonzero path between them => interpolate along that path
      ob::State *s_fromQ0 = Q0->allocState();
      ExtractQ0Subspace(q_from->state, s_fromQ0);
      Configuration *q_fromQ0 = new Configuration(Q0, s_fromQ0);

      ob::State *s_toQ0 = Q0->allocState();
      ExtractQ0Subspace(q_to->state, s_toQ0);
      Configuration *q_toQ0 = new Configuration(Q0, s_toQ0);

      //distance along cover path
      double d = 0;
      std::vector<double> d_vec;
      double d0 = parent_chart->DistanceConfigurationConfiguration(q_fromQ0, path_Q0_cover.at(0));
      d_vec.push_back(d0); d+=d0;
      for(uint k = 1; k < path_Q0_cover.size(); k++){
        double dk = parent_chart->DistanceConfigurationConfiguration(path_Q0_cover.at(k-1), path_Q0_cover.at(k));
        d_vec.push_back(dk); d+=dk;
      }
      double d1= parent_chart->DistanceConfigurationConfiguration(path_Q0_cover.back(), q_toQ0);
      d_vec.push_back(d1); d+=d1;

      q_toQ0->Remove(Q0);
      q_fromQ0->Remove(Q0);
      Q0->freeState(s_toQ0);
      Q0->freeState(s_fromQ0);

      //interpolate on X1
      ob::State *s_fromX1 = X1->allocState();
      ExtractX1Subspace(q_from->state, s_fromX1);
      ob::State *s_toX1 = X1->allocState();
      ExtractX1Subspace(q_to->state, s_toX1);

      double d_next = 0;
      path_Q1.push_back(q_from);
      for(uint k = 0; k < path_Q0_cover.size(); k++){
        ob::State *s_kX1 = X1->allocState();
        d_next += d_vec.at(k);
        X1->getStateSpace()->interpolate(s_fromX1, s_toX1, d_next/d, s_kX1);

        Configuration *qk = new Configuration(Q1);
        MergeStates(path_Q0_cover.at(k)->state, s_kX1, qk->state);
        path_Q1.push_back(qk);
        X1->freeState(s_kX1);
      }
      path_Q1.push_back(q_to);

      X1->freeState(s_toX1);
      X1->freeState(s_fromX1);
    }
  }
  return path_Q1;
}

double QuotientCover::DistanceConfigurationNeighborhood(const Configuration *q_from, const Configuration *q_to)
{
  double d_to = q_to->GetRadius();
  double d = DistanceConfigurationConfiguration(q_from, q_to);
  return std::max(d - d_to, 0.0);
}
double QuotientCover::DistanceNeighborhoodNeighborhood(const Configuration *q_from, const Configuration *q_to)
{
  double d_from = q_from->GetRadius();
  double d_to = q_to->GetRadius();
  double d = DistanceConfigurationConfiguration(q_from, q_to);

  if(d!=d){
    std::cout << std::string(80, '-') << std::endl;
    std::cout << *this << std::endl;
    std::cout << "at " << getName() << ":" << id << std::endl;
    std::cout << "NaN detected." << std::endl;
    std::cout << "d_from " << d_from << std::endl;
    std::cout << "d_to " << d_to << std::endl;
    std::cout << "d " << d << std::endl;
    std::cout << "configuration 1: " << std::endl;
    Print(q_from, false);
    std::cout << "configuration 2: " << std::endl;
    Print(q_to, false);
    std::cout << std::string(80, '-') << std::endl;
    throw "";
    exit(1);
  }
  double d_open_neighborhood_distance = std::max(d - d_from - d_to, 0.0); 
  return d_open_neighborhood_distance;
}

bool QuotientCover::GetSolution(ob::PathPtr &solution)
{
  if(!isConnected){
    Configuration* qn = Nearest(q_goal);
    double d_goal = DistanceNeighborhoodNeighborhood(qn, q_goal);
    if(d_goal < 1e-10){
      q_goal->parent_neighbor = qn;
      v_goal = AddConfigurationToCoverWithoutAddingEdges(q_goal);
      isConnected = true;
    }
  }
  if(isConnected){
    auto gpath(std::make_shared<PathGeometric>(Q1));
    shortest_path_start_goal.clear();
    shortest_path_start_goal_necessary_vertices.clear();
    shortest_path_start_goal = GetCoverPath(v_start, v_goal);
    gpath->clear();
    for(uint k = 0; k < shortest_path_start_goal.size(); k++){
      Configuration *q = graph[shortest_path_start_goal.at(k)];
      //std::cout << "vertex " << q->index << " " << (q->isSufficientFeasible?"sufficient":"") << std::endl;
      gpath->append(q->state);
      if(!q->isSufficientFeasible)
      {
        shortest_path_start_goal_necessary_vertices.push_back(shortest_path_start_goal.at(k));
      }
    }
    solution = gpath;
    return true;
  }
  return false;
}

const QuotientCover::Graph& QuotientCover::GetGraph() const
{
  return graph;
}

QuotientCover::Configuration* QuotientCover::GetStartConfiguration() const
{
  return q_start;
}
QuotientCover::Configuration* QuotientCover::GetGoalConfiguration() const
{
  return q_goal;
}

const QuotientCover::PDF& QuotientCover::GetPDFNecessaryConfigurations() const
{
  return pdf_necessary_configurations;
}

const QuotientCover::PDF& QuotientCover::GetPDFAllConfigurations() const
{
  return pdf_all_configurations;
}
const QuotientCover::NearestNeighborsPtr& QuotientCover::GetNearestNeighborsCover() const
{
  return nearest_cover;
}
const QuotientCover::NearestNeighborsPtr& QuotientCover::GetNearestNeighborsVertex() const
{
  return nearest_vertex;
}

double QuotientCover::GetGoalBias() const
{
  return goalBias;
}

struct found_goal {}; // exception for termination

template <class Vertex>
class astar_goal_visitor : public boost::default_astar_visitor
{
public:
  astar_goal_visitor(Vertex goal) : m_goal(goal) {}
  template <class Graph>
  void examine_vertex(Vertex u, Graph& g) {
    if(u == m_goal)
      throw found_goal();
  }
private:
  Vertex m_goal;
};

std::vector<const QuotientCover::Configuration*> QuotientCover::GetCoverPath(const Configuration *q_source, const Configuration *q_sink)
{
  const Vertex v_source = get(indexToVertex, q_source->index);
  const Vertex v_sink = get(indexToVertex, q_sink->index);
  std::vector<const Configuration*> q_path;
  if(v_source == v_sink) return q_path;
  std::vector<Vertex> v_path = GetCoverPath(v_source, v_sink);
  for(uint k = 0; k < v_path.size(); k++){
    q_path.push_back(graph[v_path.at(k)]);
  }
  return q_path;
}
std::vector<QuotientCover::Vertex> QuotientCover::GetCoverPath(const Vertex& v_source, const Vertex& v_sink)
{
  std::vector<Vertex> path;
  std::vector<Vertex> prev(boost::num_vertices(graph));
  auto weight = boost::make_transform_value_property_map(std::mem_fn(&EdgeInternalState::getCost), get(boost::edge_bundle, graph));
  auto predecessor = boost::make_iterator_property_map(prev.begin(), vertexToIndex);

  //check that vertices exists in graph
  if(verbose>3) std::cout << std::string(80, '-') << std::endl;
  if(verbose>3) std::cout << "searching from " << get(vertexToIndex, v_source) << " to " << get(vertexToIndex, v_sink) << std::endl;
  if(verbose>3) Q1->printState(graph[v_source]->state);
  if(verbose>3) Q1->printState(graph[v_sink]->state);
  if(verbose>3) std::cout << graph << std::endl;

  try{
    boost::astar_search(graph, v_source,
                    [this, v_sink](const Vertex &v)
                    {
                        return ob::Cost(DistanceQ1(graph[v], graph[v_sink]));
                    },
                      predecessor_map(predecessor)
                      .weight_map(weight)
                      .visitor(astar_goal_visitor<Vertex>(v_sink))
                      .vertex_index_map(vertexToIndex)
                      .distance_compare([this](EdgeInternalState c1, EdgeInternalState c2)
                                        {
                                            return opt_->isCostBetterThan(c1.getCost(), c2.getCost());
                                        })
                      .distance_combine([this](EdgeInternalState c1, EdgeInternalState c2)
                                        {
                                            return opt_->combineCosts(c1.getCost(), c2.getCost());
                                        })
                      .distance_inf(opt_->infiniteCost())
                      .distance_zero(opt_->identityCost())
                    );
  }catch(found_goal fg){
    for(Vertex v = v_sink;; v = prev[get(vertexToIndex, v)])
    {
      path.push_back(v);
      if(verbose>3)std::cout << std::string(80, '-') << std::endl;
      if(verbose>3)std::cout << "v:" << graph[v]->index << std::endl;
      if(verbose>3)std::cout << "idx:" << get(vertexToIndex, v) << std::endl;
      if(graph[prev[get(vertexToIndex, v)]]->index == graph[v]->index)
        break;
    }
    std::reverse(path.begin(), path.end());
  }
  return path;
}


PlannerDataVertexAnnotated QuotientCover::getAnnotatedVertex(ob::State* state, double radius, bool sufficient) const
{
  PlannerDataVertexAnnotated pvertex(state);
  pvertex.SetLevel(GetLevel());
  pvertex.SetOpenNeighborhoodDistance(radius);

  if(!state){
    std::cout << "vertex state does not exists" << std::endl;
    Q1->printState(state);
    exit(0);
  }

  using FeasibilityType = PlannerDataVertexAnnotated::FeasibilityType;
  if(sufficient){
    pvertex.SetFeasibility(FeasibilityType::SUFFICIENT_FEASIBLE);
  }else{
    pvertex.SetFeasibility(FeasibilityType::FEASIBLE);
  }
  return pvertex;
}
PlannerDataVertexAnnotated QuotientCover::getAnnotatedVertex(Vertex vertex) const
{
  ob::State *state = graph[vertex]->state;
  return getAnnotatedVertex(state, graph[vertex]->GetRadius(), graph[vertex]->isSufficientFeasible);
}


void QuotientCover::getPlannerData(base::PlannerData &data) const
{
  if(verbose>0) std::cout << "graph has " << boost::num_vertices(graph) << " (idx: " << index_ctr << ") vertices and " << boost::num_edges(graph) << " edges." << std::endl;
  std::map<const uint, const ob::State*> indexToStates;

  PlannerDataVertexAnnotated pstart = getAnnotatedVertex(v_start);
  indexToStates[graph[v_start]->index] = pstart.getState();
  data.addStartVertex(pstart);

  if(isConnected){
    PlannerDataVertexAnnotated pgoal = getAnnotatedVertex(v_goal);
    indexToStates[graph[v_goal]->index] = pgoal.getState();
    data.addGoalVertex(pgoal);
  }

  foreach( const Vertex v, boost::vertices(graph))
  {
    if(indexToStates.find(graph[v]->index) == indexToStates.end()) {
      PlannerDataVertexAnnotated p = getAnnotatedVertex(v);
      indexToStates[graph[v]->index] = p.getState();
      data.addVertex(p);
    }
    //otherwise vertex is a goal or start vertex and has already been added
  }
  foreach (const Edge e, boost::edges(graph))
  {
    const Vertex v1 = boost::source(e, graph);
    const Vertex v2 = boost::target(e, graph);

    const ob::State *s1 = indexToStates[graph[v1]->index];
    const ob::State *s2 = indexToStates[graph[v2]->index];
    PlannerDataVertexAnnotated p1(s1);
    PlannerDataVertexAnnotated p2(s2);
    data.addEdge(p1,p2);
  }
  if(verbose>0) std::cout << "added " << data.numVertices() << " vertices and " << data.numEdges() << " edges."<< std::endl;
}

void QuotientCover::Print(const Configuration *q, bool stopOnError) const
{
  Q1->printState(q->state);
  std::cout << " | index: " << q->index;
  std::cout << " | radius: " << q->GetRadius();
  std::cout << " | distance goal: " << q->GetGoalDistance();
  std::cout << " | coset : " << (q->coset==nullptr?"-":std::to_string(q->coset->index)) << std::endl;
  if(q->index < 0)
  {
    std::cout << "[### STATE NOT MEMBER OF COVER]" << std::endl;
  }
  if(parent != nullptr && q->coset==nullptr)
  {
    std::cout << "### STATE HAS NO COSET" << std::endl;
    if(stopOnError) exit(0);
  }
  if(q->GetRadius() < minimum_neighborhood_radius)
  {
    std::cout << "### STATE HAS ZERO-MEASURE NEIGHBORHOOD" << std::endl;
    std::cout << "### -- RADIUS OF NEIGHBORHOOD: " << q->GetRadius() << " (minimum: " << minimum_neighborhood_radius << ")" << std::endl;
    if(stopOnError) exit(0);
  }
}
void QuotientCover::Print(std::ostream& out) const
{
  BaseT::Print(out);
  out << std::endl << " |---- [Cover] has " << boost::num_vertices(graph) << " vertices and " << boost::num_edges(graph) << " edges.";
}

namespace ompl{
  namespace geometric{
    std::ostream& operator<< (std::ostream& out, const QuotientCover::Configuration& q){
      out << "[Configuration]";
      out << q.GetRadius() << std::endl;
      return out;
    }
    std::ostream& operator<< (std::ostream& out, const QuotientCover::Graph& graph)
    {
      out << std::string(80, '-') << std::endl;
      out << "[Graph]" << std::endl;
      out << std::string(80, '-') << std::endl;
      ob::SpaceInformationPtr Q1 = graph[boost::graph_bundle].Q1;

      foreach(const QuotientCover::Vertex v, boost::vertices(graph))
      {
        QuotientCover::Configuration *q = graph[v];
        std::cout << "vertex " << q->index << " radius " << q->GetRadius() << " : ";
        Q1->printState(q->state);
      }
      foreach (const QuotientCover::Edge e, boost::edges(graph))
      {
        const QuotientCover::Vertex v1 = boost::source(e, graph);
        const QuotientCover::Vertex v2 = boost::target(e, graph);
        std::cout << "edge from " << graph[v1]->index << " to " << graph[v2]->index << " (weight " << graph[e].getWeight() << ")" << std::endl;
      }

      out << std::string(80, '-') << std::endl;
      out << "--- graph " << graph[boost::graph_bundle].name << std::endl;
      out << "---       has " << boost::num_vertices(graph) << " vertices and " << boost::num_edges(graph) << " edges." << std::endl;
      out << std::string(80, '-') << std::endl;
      return out;
    }
  }
}