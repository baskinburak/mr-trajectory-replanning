#ifndef RLSS_RLSS_HPP
#define RLSS_RLSS_HPP

#include <rlss/OccupancyGrid.hpp>
#include <rlss/internal/SVM.hpp>
#include <rlss/internal/Util.hpp>
#include <splx/curve/PiecewiseCurve.hpp>
#include <splx/opt/PiecewiseCurveQPGenerator.hpp>
#include <Eigen/Geometry>
#include <Eigen/StdVector>
#include <qp_wrappers/problem.hpp>
#include <qp_wrappers/cplex.hpp>
#include <limits>

namespace rlss {

/*
*   T: field type used by the algorithm, e.g. double
*/
template<typename T, unsigned int DIM>
class RLSS {
public:
    using PiecewiseCurve = splx::PiecewiseCurve<T, DIM>;
    using PiecewiseCurveQPGenerator = splx::PiecewiseCurveQPGenerator<T, DIM>; 
    using _OccupancyGrid = rlss::OccupancyGrid<T, DIM>;
    using AlignedBox = Eigen::AlignedBox<T, DIM>;
    using VectorDIM = Eigen::Matrix<T, DIM, 1>;
    using Hyperplane = Eigen::Hyperplane<T, DIM>;
    using Vector = Eigen::Matrix<T, Eigen::Dynamic, 1>;
    using Matrix = Eigen::Matrix<T, Eigen::Dynamic, Eigen::Dynamic>;

    using StdVectorVectorDIM 
        = std::vector<VectorDIM, Eigen::aligned_allocator<VectorDIM>>;

    _OccupancyGrid& getOccupancyGrid() {
        return m_occupancy_grid;
    }

    const _OccupancyGrid& getOccupancyGrid() const {
        return m_occupancy_grid;
    }

    PiecewiseCurve& getOriginalTrajectory() {
        return m_original_trajectory;
    }

    const PiecewiseCurve& getOriginalTrajectory() const {
        return m_original_trajectory;
    }

    static AlignedBox collisionShape(
            const AlignedBox& box, const VectorDIM& position
    ) const {
        return AlignedBox(box.min() + position, box.max() + position);
    }

    AlignedBox collisionShape(const VectorDIM& position) const {
        return this->collisionShape(m_collision_box, position);
    }

    // plan for the current_time.
    PiecewiseCurve plan(
        T current_time, 
        const StdVectorVectorDIM& current_robot_state,
        const std::vector<AlignedBox>& other_robot_collision_shapes) {
    
        std::vector<Hyperplane> robot_safety_hyperplanes 
                = robotSafetyHyperplanes(
                        this->collisionShape(current_robot_state[0]),
                        other_robot_collision_shapes);

        for(const auto& bbox: other_robot_collision_shapes) {
            m_occupancy_grid.addTemporaryObstacle(bbox);
        }

        m_occupancy_grid.clearTemporaryObstacles();
    }

private:

    // collision box of the robot when CoM is at 0.
    AlignedBox m_collision_box; 

    // occupancy grid as seen by the robot
    _OccupancyGrid m_occupancy_grid;

    // original trjaectory of the robot
    PiecewiseCurve m_original_trajectory;

    // qp generator for the piecewise curve. number of pieces and their
    // number of control points are implicitly provided by this structure.
    // durations of the pieces can be adjusted by the algorithm.
    PiecewiseCurveQPGenerator m_qp_generator;

    // robots must stay in this workspace
    AlignedBox m_workspace;

    // plan should have this duration
    T m_planning_horizon;

    // plan should be safe upto this time
    T m_safe_upto;

    // maximum velocity of obstacles in the environment
    T m_max_obstacle_velocity;

    // plan should be continous up to this degree
    unsigned int m_continuity_upto;
    
    // each pair [d, lambda] denotes the lambda for integrated squared d^th
    // derivative of the curve
    std::vector<std::pair<unsigned int, T>> m_lambda_integrated_squared_derivatives;

    // each pair [d, l] denotes that magnitude of d^th derivative should
    // not be more than l
    std::vector<std::pair<unsigned int, T>> m_max_derivative_magnitudes;

    // thetas for position costs for each of the curve piece endpoints.
    // must satisfy m_theta_positions_at.size() == m_qp_generator.numPieces()
    std::vector<T> m_theta_position_at;


    static std::vector<Hyperplane> robotSafetyHyperplanes(
        const AlignedBox& robot_collision_shape
        const std::vector<AlignedBox>& other_robot_collision_shapes) const {

        std::vector<Hyperplane> hyperplanes;
        
        StdVectorVectorDIM robot_points 
            = rlss::internal::cornerPoints(robot_collision_shape);

        for(const auto& oth_collision_shape: other_robot_collision_shapes) {
            StdVectorVectorDIM oth_points 
                = rlss::internal::cornerPoints(oth_collision_shape);


            Hyperplane svm_hp = rlss::internal::svm(robot_points, oth_points);
            hyperplanes.push_back(svm_hp);
        }

        return hyperplanes;
    }

}; // class RLSS


} // namespace rlss
#endif // RLSS_RLSS_HPP