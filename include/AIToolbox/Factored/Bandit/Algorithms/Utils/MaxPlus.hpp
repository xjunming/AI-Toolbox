#ifndef AI_TOOLBOX_FACTORED_BANDIT_MAX_PLUS_HEADER_FILE
#define AI_TOOLBOX_FACTORED_BANDIT_MAX_PLUS_HEADER_FILE

#include <AIToolbox/Factored/Bandit/Types.hpp>
#include <AIToolbox/Factored/Utils/GenericVariableElimination.hpp>
#include <AIToolbox/Factored/Utils/FactorGraph.hpp>

namespace AIToolbox::Factored::Bandit {
    /**
     * @brief This class represents the Max-Plus optimization algorithm.
     *
     */
    class MaxPlus {
        public:
            using Result = std::tuple<Action, double>;

            // Value of rule, tags of processed actions
            using Factor = Vector;
            using Graph = FactorGraph<Factor>;

            /**
             * @brief This function finds the best Action-value pair for the provided QFunctionRules.
             *
             * This function automatically sets up the Graph to perform GVE on
             * from an iterable of QFunctionRules.
             *
             * @param A The action space of the agents.
             * @param rules An iterable object over QFunctionRules.
             *
             * @return A tuple containing the best Action and its value over the input rules.
             */
            template <typename Iterable>
            Result operator()(const Action & A, const Iterable & inputRules) {
                Graph graph(A.size());

                for (const auto & rule : inputRules) {
                    auto & factorNode = graph.getFactor(rule.action.first)->getData();
                    const auto id = toIndexPartial(A, rule.action);

                    const auto it = std::lower_bound(
                        std::begin(factorNode),
                        std::end(factorNode),
                        id,
                        [](const auto & rule, size_t rhs) {return rule.first < rhs;}
                    );

                    if (it != std::end(factorNode) && it->first == id)
                        it->second.first += rule.value;
                    else
                        factorNode.emplace(it, id, Factor{rule.value, {}});
                }

                return (*this)(A, graph);
            }

            /**
             * @brief This function performs the actual agent elimination process.
             *
             * For each agent, its adjacent factors, and the agents
             * adjacent to those are found. Then all possible action
             * combinations between those other agents are tried in order
             * to find the best action response be for the agent to be
             * eliminated.
             *
             * All the best responses found are added as Rules to a
             * (possibly new) factor adjacent to the adjacent agents.
             *
             * The process is repeated until all agents are eliminated.
             *
             * What remains is then joined into a single Action, containing
             * the best possible value.
             *
             * @param A The action space of the agents.
             * @param graph The graph to perform VE on.
             *
             * @return The pair for best Action and its value given the internal graph.
             */
            Result operator()(const Action & A, const Graph & graph) {
                Result retval, tmp;
                std::get<0>(retval).resize(A.size());
                std::get<1>(retval) = std::numeric_limits<double>::lowest();
                std::get<0>(tmp).resize(A.size());

                // Initialize the message cache.
                // inMessages are the previous timestep's messages sent
                // to agents, which need to be read by the factors.
                // outMessages are the messages outbound from the factors
                // to the agent.
                // We need both to avoid overwriting old messages during a
                // single message-passing step.
                std::vector<Matrix2D> inMessages(A.size()), outMessages(A.size());
                for (size_t a = 0; a < A.size(); ++a) {
                    const auto rows = graph.getFactors(a).size();
                    // Here each row corresponds to the message from
                    // a factor to this agent.
                    // We add an additional row to store the sum of all
                    // rows, to avoid having to re-sum the whole matrix
                    // all the time.
                    inMessages[a].resize(rows + 1, A[a]);
                    outMessages[a].resize(rows + 1, A[a]);

                    // We don't need to zero inMessages, we do it at the start
                    // of the while after the swap.
                    outMessages[a].setZero();
                }

                for (size_t iters = 0; iters < 10; ++iters) { // FIXME: parameter
                    std::swap(inMessages, outMessages);
                    for (auto & m : outMessages)
                        m.setZero();

                    for (auto f = graph.begin(); f != graph.end(); ++f) {
                        const auto & aNeighbors = graph.getVariables(f);

                        // Merge all in-messages together with the actual factor.
                        Vector message = f->getData(); // FIXME, reserve memory
                        size_t len = 1;
                        for (auto a : aNeighbors) {
                            const auto & fNeighbors = graph.getFactors(a);
                            const auto fId = std::distance(std::begin(fNeighbors), std::find(std::begin(fNeighbors), std::end(fNeighbors), f));

                            // Remove from sum the message coming from this factor
                            inMessages[a].bottomRows(1) -= inMessages[a].row(fId);

                            // Add each element of the message in the correct place for the
                            // cross-sum across all agents. This code is basically equivalent to
                            //
                            //     message += np.tile(np.repeat(inMessage, len), ...)
                            int i = 0;
                            while (i < message.size())
                                for (size_t j = 0; j < A[a]; ++j)
                                    for (size_t l = 0; l < len; ++l)
                                        message[i++] += inMessages[a].bottomRows(1)[j];

                            // Restore sum message for later
                            inMessages[a].bottomRows(1) += inMessages[a].row(fId);

                            len *= A[a];
                        }

                        // Send out-messages to each connected agent, maximizing the others.
                        for (auto a : aNeighbors) {
                            const auto & fNeighbors = graph.getFactors(a);
                            const auto fId = std::distance(std::begin(fNeighbors), std::find(std::begin(fNeighbors), std::end(fNeighbors), f));
                            for (size_t av = 0; av < A[a]; ++av) {
                                // Here we list all joint-action ids where the
                                // action of agent 'a' is equal to 'av'. We
                                // pick the highest and put it in the correct outMessage.
                                PartialIndexEnumerator e(A, aNeighbors, a, av);

                                double outMessage = std::numeric_limits<double>::lowest();
                                while (e.isValid()) {
                                    outMessage = std::max(outMessage, message[*e]);
                                    e.advance();
                                }
                                outMessages[a](fId, av) = outMessage;
                            }
                        }
                    }

                    // Finally check whether we have a new best action, and
                    // compute the messages for the next iteration.
                    auto & [rAction, rValue] = retval;
                    auto & [cAction, cValue] = tmp;

                    cValue = 0.0;
                    for (size_t a = 0; a < A.size(); ++a) {
                        auto & m = outMessages[a];
                        // Normalization constant to handle loopy graphs.
                        m.array() -= m.sum() / (m.rows() + m.cols() - 1);

                        // Compute "generic" message, we subtract each row one
                        // at a time when it is needed. Note that we need to
                        // avoid summing the last row since we have
                        // "incorrectly" subtracted the normalization from it.
                        m.bottomRows(1) = m.topRows(m.rows()-1).rowwise().sum();

                        cValue += m.bottomRows(1).maxCoeff(&cAction[a]);
                    }

                    // We only change the selected action if it improves on
                    // the previous best value.
                    if (cValue > rValue) {
                        rValue = cValue;
                        rAction = cAction;
                    }
                }
                return retval;
            }
    };
}

#endif
