#ifndef RLSS_RLSS_SOFT_OPTIMIZER_HPP
#define RLSS_RLSS_SOFT_OPTIMIZER_HPP

#include <rlss/TrajectoryOptimizers/TrajectoryOptimizer.hpp>
#include <rlss/internal/Util.hpp>

namespace rlss {

template<typename T, unsigned int DIM>
class RLSSSoftOptimizer: public TrajectoryOptimizer<T, DIM> {
public:
    using Base = TrajectoryOptimizer<T, DIM>;
    using StdVectorVectorDIM = typename Base::StdVectorVectorDIM;
    using AlignedBox = typename Base::AlignedBox;
    using OccupancyGrid = typename Base::OccupancyGrid;
    using PiecewiseCurve = typename Base::PiecewiseCurve;
    using PiecewiseCurveQPGenerator = splx::PiecewiseCurveQPGenerator<T, DIM>;
    using VectorDIM = rlss::internal::VectorDIM<T, DIM>;
    using CollisionShape = rlss::CollisionShape<T, DIM>;
    using Hyperplane = rlss::internal::Hyperplane<T, DIM>;
    using Vector = typename PiecewiseCurveQPGenerator::Vector;

    RLSSSoftOptimizer(
            std::shared_ptr<CollisionShape> colshape,
        const PiecewiseCurveQPGenerator& qpgen,
        const AlignedBox& ws,
        unsigned int contupto,
        const std::vector<std::pair<unsigned int, T>>& lambdas,
        const std::vector<T>& thetas
        ): m_collision_shape(colshape),
        m_qp_generator(qpgen),
        m_workspace(ws),
        m_continuity_upto(contupto),
        m_lambda_integrated_squared_derivatives(lambdas),
        m_theta_position_at(thetas)
    {

    }

    // returns std::nullopt when optimization fails
    std::optional<PiecewiseCurve> optimize(
            const StdVectorVectorDIM& segments,
            const std::vector<T>& durations,
            const std::vector<AlignedBox>& oth_rbt_col_shape_bboxes,
            const OccupancyGrid& occupancy_grid,
            const StdVectorVectorDIM& current_robot_state
    )  override {
        assert(segments.size() == m_qp_generator.numPieces() + 1);
        assert(durations.size() == segments.size() - 1);
        assert(current_robot_state.size() > m_continuity_upto);

        internal::MathematicaWriter<T, DIM> mathematica;

        for(const auto& bbox: oth_rbt_col_shape_bboxes) {
            debug_message(
                    "other robot collision shape bounding box is [min: ",
                    bbox.min().transpose(),
                    ", max: ",
                    bbox.max().transpose(),
                    "]"
            );
            mathematica.otherRobotCollisionBox(bbox);
        }

        m_qp_generator.resetProblem();
        m_qp_generator.setPieceMaxParameters(durations);

        mathematica.discretePath(segments);

        // workspace constraint
        AlignedBox ws = rlss::internal::bufferAlignedBox<T, DIM>(
                VectorDIM::Zero(),
                m_collision_shape->boundingBox(VectorDIM::Zero()),
                m_workspace
        );
        m_qp_generator.addBoundingBoxConstraint(ws);
        debug_message(
                "buffered workspace is [min: ",
                ws.min().transpose(),
                ", max: ",
                ws.max().transpose(),
                "]"
        );


        mathematica.selfCollisionBox(
                m_collision_shape->boundingBox(current_robot_state[0]));

        // robot to robot avoidance constraints for the first piece
        std::vector<Hyperplane> robot_to_robot_hps
                = this->robotSafetyHyperplanes(
                        current_robot_state[0],
                        oth_rbt_col_shape_bboxes
                );
        for(const auto& hp: robot_to_robot_hps) {
            m_qp_generator.addHyperplaneConstraintForPiece(0, hp, true, 10000);
            if(hp.signedDistance(current_robot_state[0]) > 0) {
                debug_message(
                        "distance of current point to a first piece hyperplane: ",
                        hp.signedDistance(current_robot_state[0]));
            }
        }


        assert(robot_to_robot_hps.size() == oth_rbt_col_shape_bboxes.size());

        for(const auto& hp: robot_to_robot_hps) {
            mathematica.robotCollisionAvoidanceHyperplane(hp);
            debug_message(
                    "robot to robot collision avoidance hyperplane [n: ",
                    hp.normal().transpose(),
                    ", d: ",
                    hp.offset(),
                    "]"
            );
        }


        // robot to obstacle avoidance constraints for all pieces
        for (
                std::size_t p_idx = 0;
                p_idx < m_qp_generator.numPieces();
                p_idx++
                ) {
            AlignedBox from_box
                    = m_collision_shape->boundingBox(segments[p_idx]);
            AlignedBox to_box
                    = m_collision_shape->boundingBox(segments[p_idx+1]);
            to_box.extend(from_box);

            StdVectorVectorDIM segments_corners
                    = rlss::internal::cornerPoints<T, DIM>(to_box);

            for(
                    auto it = occupancy_grid.begin();
                    it != occupancy_grid.end();
                    ++it
                    ) {

                AlignedBox grid_box = *it;

                StdVectorVectorDIM grid_box_corners
                        = rlss::internal::cornerPoints<T, DIM>(grid_box);

                Hyperplane shp = rlss::internal::svm<T, DIM>
                        (
                                segments_corners,
                                grid_box_corners
                        );


                shp = rlss::internal::shiftHyperplane<T, DIM>(
                        VectorDIM::Zero(),
                        m_collision_shape->boundingBox(VectorDIM::Zero()),
                        shp
                );


                if(p_idx == 0) {
                    mathematica.obstacleCollisionBox(grid_box);
//                    mathematica.obstacleCollisionAvoidanceHyperplane(shp);
                    if(shp.signedDistance(current_robot_state[0]) > 0) {
                        debug_message(
                                "distance of current point to a first piece hyperplane: ",
                                shp.signedDistance(current_robot_state[0])
                        );
                    }
                }
                m_qp_generator.addHyperplaneConstraintForPiece(p_idx, shp, true, 10000);
            }
        }


        // continuity constraints
        for(
                std::size_t p_idx = 0;
                p_idx < m_qp_generator.numPieces() - 1;
                p_idx++
                ) {
            for(unsigned int k = 0; k <= m_continuity_upto; k++) {
                debug_message(
                        "adding continuity constraint between piece ",
                        p_idx,
                        " and ",
                        p_idx+1,
                        " for degree ",
                        k
                );
                m_qp_generator.addContinuityConstraint(p_idx, k, true, 100000);
            }
        }

        // initial point constraints
        for(unsigned int k = 0; k <= m_continuity_upto; k++) {
            debug_message(
                    "adding initial point constraint for degree ",
                    k,
                    ". It should be ",
                    current_robot_state[k].transpose()
            );
            m_qp_generator.addEvalConstraint(0, k, current_robot_state[k], true, 1000000);
        }



        // energy cost
        for(const auto& [d, l]: m_lambda_integrated_squared_derivatives) {
            debug_message("adding integrated squared derivative cost for",
                          "degree ", d, " with lambda ", l
            );
            m_qp_generator.addIntegratedSquaredDerivativeCost(d, l);
        }



        // eval cost
        T duration_sum_before = 0;
        for(
                std::size_t p_idx = 0;
                p_idx < m_qp_generator.numPieces();
                duration_sum_before += durations[p_idx], p_idx++
                ) {
            m_qp_generator.addEvalCost(
                    std::min(
                            duration_sum_before + durations[p_idx],
                            m_qp_generator.maxParameter()
                    ),
                    0,
                    segments[p_idx+1],
                    m_theta_position_at[p_idx]
            );
        }


        auto initial_guess = m_qp_generator.getDVarsForSegments(segments);
        auto problem = m_qp_generator.getProblem()
                            .convert_to_soft();
        debug_message("hard num vars: "
                    , m_qp_generator.getProblem().num_vars()
                    , ", soft num vars: "
                    , problem.num_vars());
        auto soft_initial_guess = Vector(problem.num_vars());
        soft_initial_guess.setZero();
        soft_initial_guess.block(0, 0, initial_guess.cols(), 1) = initial_guess;


        QPWrappers::RLSS_SOFT_QP_SOLVER::Engine<T> solver;
        solver.setFeasibilityTolerance(1e-9);
        Vector soln;
        auto ret = solver.next(
                problem, soln,  soft_initial_guess);
        debug_message("optimization return value: ", ret);
        if(ret == QPWrappers::OptReturnType::Optimal) {
            debug_message("slack variables: ",
              soln.block(m_qp_generator.getProblem().num_vars(),
                         0,
                         soln.rows() - m_qp_generator.getProblem().num_vars(),
                         1).transpose()
            );
            Vector soln_primary
                = soln.block(0, 0, m_qp_generator.numDecisionVariables(), 1);
            debug_message("primary variables: ", soln_primary. transpose());
            auto result = m_qp_generator.extractCurve(soln_primary);
            mathematica.piecewiseCurve(result);
            return result;
        } else {
            return std::nullopt;
        }
    }
private:
    std::shared_ptr<CollisionShape> m_collision_shape;
    PiecewiseCurveQPGenerator m_qp_generator;
    AlignedBox m_workspace;
    unsigned int m_continuity_upto;
    std::vector<std::pair<unsigned int, T>>
            m_lambda_integrated_squared_derivatives;
    std::vector<T> m_theta_position_at;

    std::vector<Hyperplane> robotSafetyHyperplanes(
            const VectorDIM& robot_position,
            const std::vector<AlignedBox>&
            other_robot_collision_shape_bounding_boxes) const {

        std::vector<Hyperplane> hyperplanes;

        AlignedBox robot_box
                = m_collision_shape->boundingBox(robot_position);

        StdVectorVectorDIM robot_points
                = rlss::internal::cornerPoints<T, DIM>(robot_box);

        for(const auto& oth_collision_shape_bbox:
                other_robot_collision_shape_bounding_boxes) {
            StdVectorVectorDIM oth_points
                    = rlss::internal::cornerPoints<T, DIM>(oth_collision_shape_bbox);


            Hyperplane svm_hp = rlss::internal::svm<T, DIM>(robot_points, oth_points);


            Hyperplane svm_shifted = rlss::internal::shiftHyperplane<T, DIM>(
                    robot_position,
                    robot_box,
                    svm_hp
            );

            hyperplanes.push_back(svm_shifted);
        }

        return hyperplanes;
    }
}; // RLSSSoftOptimizer

} // namespace rlss

#endif // RLSS_RLSS_OPTIMIZER_HPP