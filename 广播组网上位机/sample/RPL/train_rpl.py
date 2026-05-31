import gc
import os
import random

import gym
import gym_environments
import numpy as np
import torch
from tqdm import tqdm

from rpl import RPLAgent, device, evaluation_interval, listofDemands


print(f"device={device}")

ENV_NAME = "GraphEnv-v1"
graph_topology = 0
SEED = int(os.environ.get("RPL_SEED", "37"))

ITERATIONS = int(os.environ.get("RPL_ITERATIONS", "2000"))
TRAINING_EPISODES = int(os.environ.get("RPL_TRAINING_EPISODES", "20"))
EVALUATION_EPISODES = int(os.environ.get("RPL_EVALUATION_EPISODES", "20"))
FIRST_WORK_TRAIN_EPISODE = int(os.environ.get("RPL_FIRST_WORK_TRAIN_EPISODE", "60"))
RUN_EVALUATION = EVALUATION_EPISODES > 0

os.environ["PYTHONHASHSEED"] = str(SEED)
np.random.seed(SEED)
random.seed(SEED)
torch.manual_seed(1)
if torch.cuda.is_available():
    torch.cuda.manual_seed_all(1)
    torch.backends.cudnn.deterministic = True
    torch.backends.cudnn.benchmark = False


def evaluate_agent(agent, env_eval):
    rewards_test = np.zeros(EVALUATION_EPISODES)
    test_packet_loss_rate = []
    test_total_delay = []
    test_link_utilization = []
    test_global_link_utilization = []
    test_max_link_utilization = []
    test_congested_link_ratio = []
    test_propagation_delay = []
    test_transmission_delay = []
    test_queueing_delay = []
    test_decomposed_total_delay = []
    test_route_length_hops = []
    test_mean_delay_per_hop = []
    test_route_distance = []

    for eps in tqdm(range(EVALUATION_EPISODES)):
        state, demand, source, destination = env_eval.reset()
        reward_add_test = 0.0
        packet_loss_rate_test = []
        total_delay_test = 0.0
        link_utilization_test = []
        global_link_utilization_test = []
        max_link_utilization_test = []
        congested_link_ratio_test = []
        propagation_delay_test = 0.0
        transmission_delay_test = 0.0
        queueing_delay_test = 0.0
        decomposed_total_delay_test = 0.0
        route_length_hops_test = []
        mean_delay_per_hop_test = []
        route_distance_test = []

        while True:
            action, _ = agent.act(env_eval, state, demand, source, destination, True)
            new_state, reward, done, demand, source, destination, info = env_eval.make_step(
                state,
                action,
                demand,
                source,
                destination,
            )

            reward_add_test += reward
            state = new_state
            packet_loss_rate_test.append(info["packet_loss_rate"])
            total_delay_test += info["total_delay"]
            link_utilization_test.append(info["link_utilization"])
            global_link_utilization_test.append(info["global_link_utilization"])
            max_link_utilization_test.append(info["max_link_utilization"])
            congested_link_ratio_test.append(info["congested_link_ratio"])
            propagation_delay_test += info.get("propagation_delay", 0.0)
            transmission_delay_test += info.get("transmission_delay", 0.0)
            queueing_delay_test += info.get("queueing_delay", 0.0)
            decomposed_total_delay_test += info.get("decomposed_total_delay", 0.0)
            route_length_hops_test.append(info.get("route_length_hops", 0.0))
            mean_delay_per_hop_test.append(info.get("mean_delay_per_hop", 0.0))
            route_distance_test.append(info.get("route_distance", 0.0))
            if done:
                break

        rewards_test[eps] = reward_add_test
        test_packet_loss_rate.append(float(np.mean(packet_loss_rate_test)))
        test_total_delay.append(float(total_delay_test))
        test_link_utilization.append(float(np.mean(link_utilization_test)))
        test_global_link_utilization.append(float(np.mean(global_link_utilization_test)))
        test_max_link_utilization.append(float(np.mean(max_link_utilization_test)))
        test_congested_link_ratio.append(float(np.mean(congested_link_ratio_test)))
        test_propagation_delay.append(float(propagation_delay_test))
        test_transmission_delay.append(float(transmission_delay_test))
        test_queueing_delay.append(float(queueing_delay_test))
        test_decomposed_total_delay.append(float(decomposed_total_delay_test))
        test_route_length_hops.append(float(np.mean(route_length_hops_test)))
        test_mean_delay_per_hop.append(float(np.mean(mean_delay_per_hop_test)))
        test_route_distance.append(float(np.mean(route_distance_test)))

    return (
        float(np.mean(rewards_test)),
        float(np.mean(test_packet_loss_rate)),
        float(np.mean(test_total_delay)),
        float(np.mean(test_link_utilization)),
        float(np.mean(test_global_link_utilization)),
        float(np.mean(test_max_link_utilization)),
        float(np.mean(test_congested_link_ratio)),
        float(np.mean(test_propagation_delay)),
        float(np.mean(test_transmission_delay)),
        float(np.mean(test_queueing_delay)),
        float(np.mean(test_decomposed_total_delay)),
        float(np.mean(test_route_length_hops)),
        float(np.mean(test_mean_delay_per_hop)),
        float(np.mean(test_route_distance)),
    )


if __name__ == "__main__":
    env_training = gym.make(ENV_NAME, disable_env_checker=True)
    np.random.seed(SEED)
    env_training.seed(SEED)
    env_training.generate_environment(graph_topology, listofDemands)

    env_eval = gym.make(ENV_NAME, disable_env_checker=True)
    np.random.seed(SEED)
    env_eval.seed(SEED)
    env_eval.generate_environment(graph_topology, listofDemands)

    agent = RPLAgent(env_training)
    max_reward = float("-inf")
    counter_store_model = 1

    if RUN_EVALUATION:
        initial_eval_reward, *_ = evaluate_agent(agent, env_eval)
        max_reward = max(max_reward, initial_eval_reward)

    for ep_it in tqdm(range(ITERATIONS)):
        if ep_it % 5 == 0:
            print("Training iteration:", ep_it)

        train_episodes = FIRST_WORK_TRAIN_EPISODE if ep_it == 0 else TRAINING_EPISODES

        train_reward = []
        train_packet_loss_rate = []
        train_total_delay = []
        train_link_utilization = []
        train_global_link_utilization = []
        train_max_link_utilization = []
        train_congested_link_ratio = []
        train_propagation_delay = []
        train_transmission_delay = []
        train_queueing_delay = []
        train_decomposed_total_delay = []
        train_route_length_hops = []
        train_mean_delay_per_hop = []
        train_route_distance = []
        train_route_length_samples = []

        for _ in range(train_episodes):
            torch.manual_seed(1)
            state, demand, source, destination = env_training.reset()
            reward_add_train = 0.0
            packet_loss_values = []
            total_delay_value = 0.0
            link_utilization_values = []
            global_link_utilization_values = []
            max_link_utilization_values = []
            congested_link_ratio_values = []
            propagation_delay_value = 0.0
            transmission_delay_value = 0.0
            queueing_delay_value = 0.0
            decomposed_total_delay_value = 0.0
            route_length_hops_values = []
            mean_delay_per_hop_values = []
            route_distance_values = []

            while True:
                action, state_action = agent.act(env_training, state, demand, source, destination, False)
                new_state, reward, done, new_demand, new_source, new_destination, info = env_training.make_step(
                    state,
                    action,
                    demand,
                    source,
                    destination,
                )

                agent.add_sample(
                    env_training,
                    state_action,
                    action,
                    reward,
                    done,
                    new_state,
                    new_demand,
                    new_source,
                    new_destination,
                )

                packet_loss_values.append(info["packet_loss_rate"])
                total_delay_value += info["total_delay"]
                link_utilization_values.append(info["link_utilization"])
                global_link_utilization_values.append(info["global_link_utilization"])
                max_link_utilization_values.append(info["max_link_utilization"])
                congested_link_ratio_values.append(info["congested_link_ratio"])
                propagation_delay_value += info.get("propagation_delay", 0.0)
                transmission_delay_value += info.get("transmission_delay", 0.0)
                queueing_delay_value += info.get("queueing_delay", 0.0)
                decomposed_total_delay_value += info.get("decomposed_total_delay", 0.0)
                route_length_hops_values.append(info.get("route_length_hops", 0.0))
                mean_delay_per_hop_values.append(info.get("mean_delay_per_hop", 0.0))
                route_distance_values.append(info.get("route_distance", 0.0))
                reward_add_train += reward

                state = new_state
                demand = new_demand
                source = new_source
                destination = new_destination

                if done:
                    break

            train_reward.append(reward_add_train)
            train_packet_loss_rate.append(float(np.mean(packet_loss_values)))
            train_total_delay.append(float(total_delay_value))
            train_link_utilization.append(float(np.mean(link_utilization_values)))
            train_global_link_utilization.append(float(np.mean(global_link_utilization_values)))
            train_max_link_utilization.append(float(np.mean(max_link_utilization_values)))
            train_congested_link_ratio.append(float(np.mean(congested_link_ratio_values)))
            train_propagation_delay.append(float(propagation_delay_value))
            train_transmission_delay.append(float(transmission_delay_value))
            train_queueing_delay.append(float(queueing_delay_value))
            train_decomposed_total_delay.append(float(decomposed_total_delay_value))
            train_route_length_hops.append(float(np.mean(route_length_hops_values)))
            train_mean_delay_per_hop.append(float(np.mean(mean_delay_per_hop_values)))
            train_route_distance.append(float(np.mean(route_distance_values)))
            train_route_length_samples.extend(float(value) for value in route_length_hops_values)

        agent.replay(ep_it)

        mean_train_reward = float(np.mean(train_reward))
        mean_packet_loss_rate = float(np.mean(train_packet_loss_rate))
        mean_total_delay = float(np.mean(train_total_delay))
        mean_link_utilization = float(np.mean(train_link_utilization))
        mean_global_link_utilization = float(np.mean(train_global_link_utilization))
        mean_max_link_utilization = float(np.mean(train_max_link_utilization))
        mean_congested_link_ratio = float(np.mean(train_congested_link_ratio))
        mean_propagation_delay = float(np.mean(train_propagation_delay))
        mean_transmission_delay = float(np.mean(train_transmission_delay))
        mean_queueing_delay = float(np.mean(train_queueing_delay))
        mean_decomposed_total_delay = float(np.mean(train_decomposed_total_delay))
        mean_route_length_hops = float(np.mean(train_route_length_hops))
        mean_mean_delay_per_hop = float(np.mean(train_mean_delay_per_hop))
        mean_route_distance = float(np.mean(train_route_distance))
        train_score = mean_total_delay + 10.0 * mean_packet_loss_rate

        agent.writer.add_scalar("train_reward", mean_train_reward, ep_it)
        agent.writer.add_scalar("packet_loss_rate", mean_packet_loss_rate, ep_it)
        agent.writer.add_scalar("total_delay", mean_total_delay, ep_it)
        agent.writer.add_scalar("link_utilization", mean_link_utilization, ep_it)
        agent.writer.add_scalar("global_link_utilization", mean_global_link_utilization, ep_it)
        agent.writer.add_scalar("max_link_utilization", mean_max_link_utilization, ep_it)
        agent.writer.add_scalar("congested_link_ratio", mean_congested_link_ratio, ep_it)
        agent.writer.add_scalar("propagation_delay", mean_propagation_delay, ep_it)
        agent.writer.add_scalar("transmission_delay", mean_transmission_delay, ep_it)
        agent.writer.add_scalar("queueing_delay", mean_queueing_delay, ep_it)
        agent.writer.add_scalar("decomposed_total_delay", mean_decomposed_total_delay, ep_it)
        agent.writer.add_scalar("route_length_hops", mean_route_length_hops, ep_it)
        agent.writer.add_scalar("mean_delay_per_hop", mean_mean_delay_per_hop, ep_it)
        agent.writer.add_scalar("route_distance", mean_route_distance, ep_it)
        if train_route_length_samples:
            agent.writer.add_histogram(
                "route_length_hops_distribution",
                np.asarray(train_route_length_samples, dtype=np.float32),
                ep_it,
            )
        agent.writer.add_scalar("train_score", train_score, ep_it)

        if RUN_EVALUATION and ep_it % evaluation_interval == 0:
            (
                eval_reward,
                eval_packet_loss_rate,
                eval_total_delay,
                eval_link_utilization,
                eval_global_link_utilization,
                eval_max_link_utilization,
                eval_congested_link_ratio,
                eval_propagation_delay,
                eval_transmission_delay,
                eval_queueing_delay,
                eval_decomposed_total_delay,
                eval_route_length_hops,
                eval_mean_delay_per_hop,
                eval_route_distance,
            ) = evaluate_agent(agent, env_eval)
            max_reward = max(max_reward, eval_reward)

            agent.writer.add_scalar("eval_reward", eval_reward, ep_it)
            agent.writer.add_scalar("eval_packet_loss_rate", eval_packet_loss_rate, ep_it)
            agent.writer.add_scalar("eval_total_delay", eval_total_delay, ep_it)
            agent.writer.add_scalar("eval_link_utilization", eval_link_utilization, ep_it)
            agent.writer.add_scalar("eval_global_link_utilization", eval_global_link_utilization, ep_it)
            agent.writer.add_scalar("eval_max_link_utilization", eval_max_link_utilization, ep_it)
            agent.writer.add_scalar("eval_congested_link_ratio", eval_congested_link_ratio, ep_it)
            agent.writer.add_scalar("eval_propagation_delay", eval_propagation_delay, ep_it)
            agent.writer.add_scalar("eval_transmission_delay", eval_transmission_delay, ep_it)
            agent.writer.add_scalar("eval_queueing_delay", eval_queueing_delay, ep_it)
            agent.writer.add_scalar("eval_decomposed_total_delay", eval_decomposed_total_delay, ep_it)
            agent.writer.add_scalar("eval_route_length_hops", eval_route_length_hops, ep_it)
            agent.writer.add_scalar("eval_mean_delay_per_hop", eval_mean_delay_per_hop, ep_it)
            agent.writer.add_scalar("eval_route_distance", eval_route_distance, ep_it)
            counter_store_model += 1

        gc.collect()

    if RUN_EVALUATION:
        (
            final_eval_reward,
            final_eval_packet_loss_rate,
            final_eval_total_delay,
            final_eval_link_utilization,
            final_eval_global_link_utilization,
            final_eval_max_link_utilization,
            final_eval_congested_link_ratio,
            final_eval_propagation_delay,
            final_eval_transmission_delay,
            final_eval_queueing_delay,
            final_eval_decomposed_total_delay,
            final_eval_route_length_hops,
            final_eval_mean_delay_per_hop,
            final_eval_route_distance,
        ) = evaluate_agent(agent, env_eval)
        max_reward = max(max_reward, final_eval_reward)

        agent.writer.add_scalar("final_eval_reward", final_eval_reward, counter_store_model)
        agent.writer.add_scalar("final_eval_packet_loss_rate", final_eval_packet_loss_rate, counter_store_model)
        agent.writer.add_scalar("final_eval_total_delay", final_eval_total_delay, counter_store_model)
        agent.writer.add_scalar("final_eval_link_utilization", final_eval_link_utilization, counter_store_model)
        agent.writer.add_scalar(
            "final_eval_global_link_utilization",
            final_eval_global_link_utilization,
            counter_store_model,
        )
        agent.writer.add_scalar("final_eval_max_link_utilization", final_eval_max_link_utilization, counter_store_model)
        agent.writer.add_scalar(
            "final_eval_congested_link_ratio",
            final_eval_congested_link_ratio,
            counter_store_model,
        )
        agent.writer.add_scalar("final_eval_propagation_delay", final_eval_propagation_delay, counter_store_model)
        agent.writer.add_scalar("final_eval_transmission_delay", final_eval_transmission_delay, counter_store_model)
        agent.writer.add_scalar("final_eval_queueing_delay", final_eval_queueing_delay, counter_store_model)
        agent.writer.add_scalar(
            "final_eval_decomposed_total_delay",
            final_eval_decomposed_total_delay,
            counter_store_model,
        )
        agent.writer.add_scalar("final_eval_route_length_hops", final_eval_route_length_hops, counter_store_model)
        agent.writer.add_scalar("final_eval_mean_delay_per_hop", final_eval_mean_delay_per_hop, counter_store_model)
        agent.writer.add_scalar("final_eval_route_distance", final_eval_route_distance, counter_store_model)

    agent.writer.flush()
    agent.writer.close()
