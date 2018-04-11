#ifndef PATHREPLAN_OPTIMIZATION_H
#define PATHREPLAN_OPTIMIZATION_H
#include "trajectory.h"
#include "obstacle.h"
#include <vector>
#include "hyperplane.h"
#include "vectoreuc.h"

using namespace std;


class problem_data {
  public:
    double current_t;
    double time_horizon;
    double tt;
    trajectory* original_trajectory;
    int problem_dimension;
    int ppc;
};

class alt_obj_data {
  public:
    problem_data* pdata;
    vectoreuc* pos;
    vectoreuc* vel;
    vectoreuc* acc;
};

/*
voronoi constraint for one point only.
*/
class voronoi_data {
  public:
    problem_data* pdata;
    hyperplane plane;
    int curve_idx;
    int point_idx;
};

class obstacle_data {
  public:
    problem_data* pdata;
    vector<hyperplane>* hps;
};

class continuity_data {
  public:
    problem_data* pdata;
    int n; // continuity degree
    int c; // first curve index. next curve is c+1
};

class point_data {
  public:
    problem_data* pdata;
    vectoreuc point;
    double time;
    int degree;
};

class maxnvalue_data {
  public:
    problem_data* pdata;
    int cidx; // curve index
    int degree; // derivative degree(1 = velocity, 2 = acc, 3= jerk etc)
    double max_val;
};

class optimization {
  public:
    static double objective(const vector<double>& x, vector<double>& grad, void* f_data);
    static double alt_objective(const vector<double>& x, vector<double>& grad, void* f_data);
    static double voronoi_constraint(const vector<double>& x, vector<double>& grad, void* v_data);
    static double obstacle_constraint(const vector<double>& x, vector<double>& grad, void* o_data);
    /* requires distance between two points to be zero, use it with add_equality_constraint*/
    static double continuity_constraint(const vector<double>& x, vector<double>& grad, void* c_data);

    /* enforces being in the given point at the given time */
    static double point_constraint(const vector<double>& x, vector<double>& grad, void* p_data);

    /*calculate the maximum value of nth derivative of given curve*/
    static double maximum_nvalue_of_curve(const vector<double>& x, vector<double>& grad, void* d_data);
};


#endif
