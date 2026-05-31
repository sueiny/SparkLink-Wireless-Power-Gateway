import numpy as np
import gym_environments
import gym
from dqn_torch import *
os.environ['CUDA_VISIBLE_DEVICES'] = '-1'
print(torch.cuda.is_available())

ENV_NAME = 'GraphEnv-v1'
graph_topology = 0 # 0==NSFNET, 1==GEANT2, 2==Small Topology, 3==GBN
SEED = 37

ITERATIONS = 2000               # 训练迭代次数
TRAINING_EPISODES = 20          # 每个训练迭代中执行的训练回合数
EVALUATION_EPISODES = 20        # 每个训练迭代中执行的评估回合数
FIRST_WORK_TRAIN_EPISODE = 60  # 初始训练回合数，用于在经验回放队列为空时进行额外训练
TAU = 0.08                     # 仅用于软权重复制的参数，控制权重更新的速度
os.environ['PYTHONHASHSEED']=str(SEED)
np.random.seed(SEED)
random.seed(SEED)
torch.manual_seed(1)
if __name__ == "__main__":
    # python train_DQN.py
    # Get the environment and extract the number of actions.
    env_training = gym.make(ENV_NAME, disable_env_checker=True)
    np.random.seed(SEED)
    env_training.seed(SEED)
    env_training.generate_environment(graph_topology, listofDemands)

    env_eval = gym.make(ENV_NAME, disable_env_checker=True)
    np.random.seed(SEED)
    env_eval.seed(SEED)
    env_eval.generate_environment(graph_topology, listofDemands)

    batch_size = hparams['batch_size']
    agent = DQNAgent(env_training,batch_size)

    eval_ep = 0
    train_ep = 0
    max_reward = 0
    reward_id = 0


    checkpoint = {'model_state_dict': agent.primary_network.state_dict(),
                  'optimizer_state_dict': agent.optimizer.state_dict()}

    rewards_test = np.zeros(EVALUATION_EPISODES)

    # for eps in tqdm(range(EVALUATION_EPISODES)):
    #     state, demand, source, destination = env_eval.reset()
    #     rewardAddTest = 0
    #     while 1:
    #         # We execute evaluation over current state
    #         # demand, src, dst
    #         action, _ = agent.act(env_eval, state, demand, source, destination, True)

    #         new_state, reward, done, demand, source, destination,info = env_eval.make_step(state, action, demand, source,
    #                                                                                   destination)
    #         rewardAddTest = rewardAddTest + reward
    #         state = new_state
    #         if done:
    #             break
    #     rewards_test[eps] = rewardAddTest

    # evalMeanReward = np.mean(rewards_test)
    # fileLogs.write(">," + str(evalMeanReward) + ",\n")
    # fileLogs.write("-," + str(agent.epsilon) + ",\n")
    # fileLogs.flush()

    counter_store_model = 1

    for ep_it in tqdm(range(ITERATIONS)):

        if ep_it % 5 == 0: # 每5次迭代打印训练进度
            print("Training iteration: ", ep_it)
        if ep_it == 0: # 在开始时，经验回放队列为空，因此强制执行更多的训练回合
            train_episodes = FIRST_WORK_TRAIN_EPISODE
        else:
            train_episodes = TRAINING_EPISODES

        train_reward = []    # 初始化训练奖励列表
        train_packet_loss_rate_train = []  # 初始化训练包丢失率列表
        train_total_delay = []   # 初始化训练总延迟列表
        train_link_utilization = []   # 初始化训练链路利用率列表
        for _ in range(train_episodes):
            torch.manual_seed(1)
            state, demand, source, destination = env_training.reset()  # 重置环境并获取初始状态
            rewardAddTrain = 0
            packet_loss_rate_train = []
            total_delay_train = 0
            link_utilization_train = []
            while 1:
                # We execute evaluation over current state
                action, state_action = agent.act(env_training, state, demand, source, destination, False)
                new_state, reward, done, new_demand, new_source, new_destination, info = env_training.make_step(state,
                                                                                                                action,
                                                                                                                demand,
                                                                                                                source,
                                                                                                                destination)
                
                agent.add_sample(env_training, state_action, action, reward, done, new_state, new_demand, new_source,
                                 new_destination)
                packet_loss_rate = info['packet_loss_rate']
                total_delay = info['total_delay']
                link_utilization = info['link_utilization']
                packet_loss_rate_train.append(packet_loss_rate)
                total_delay_train += total_delay
                link_utilization_train.append(link_utilization)
                state = new_state
                demand = new_demand
                source = new_source
                destination = new_destination
                rewardAddTrain += reward
                if done:
                    break
            train_reward.append(rewardAddTrain)
            train_packet_loss_rate_train.append(np.mean(packet_loss_rate_train))
            train_total_delay.append(total_delay_train)
            train_link_utilization.append(np.mean(link_utilization_train))
        agent.replay(ep_it)
        mean_train_reward = np.mean(train_reward)
        agent.writer.add_scalar("train_reward", mean_train_reward, ep_it)
        agent.writer.add_scalar("packet_loss_rate", np.mean(train_packet_loss_rate_train), ep_it)
        agent.writer.add_scalar("total_delay", np.mean(train_total_delay), ep_it)
        agent.writer.add_scalar("link_utilization", np.mean(train_link_utilization), ep_it)
        if ep_it > epsilon_start_decay and agent.epsilon > agent.epsilon_min:
            agent.epsilon *= agent.epsilon_decay  # 衰减epsilon值

        # 评估模型
        if ep_it % evaluation_interval == 0:
            test_packet_loss_rate = []
            test_total_delay = []
            test_link_utilization = []
            for eps in tqdm(range(EVALUATION_EPISODES)):
                state, demand, source, destination = env_eval.reset()
                rewardAddTest = 0
                packet_loss_rate_test = []
                total_delay_test = 0
                link_utilization_test = []
                while 1:
                    # We execute evaluation over current state
                    action, _ = agent.act(env_eval, state, demand, source, destination, True)

                    new_state, reward, done, demand, source, destination, info = env_eval.make_step(state, action,
                                                                                                    demand, source,
                                                                                                    destination)
                    rewardAddTest = rewardAddTest + reward
                    state = new_state
                    packet_loss_rate = info['packet_loss_rate']
                    total_delay = info['total_delay']
                    link_utilization = info['link_utilization']
                    packet_loss_rate_test.append(packet_loss_rate)
                    total_delay_test += total_delay
                    link_utilization_test.append(link_utilization)
                    if done:
                        break
                rewards_test[eps] = rewardAddTest
            evalMeanReward = np.mean(rewards_test)
            test_packet_loss_rate.append(np.mean(packet_loss_rate_test))
            test_total_delay.append(total_delay_test)
            test_link_utilization.append(np.mean(link_utilization_test))
            if evalMeanReward > max_reward:
                max_reward = evalMeanReward
                reward_id = counter_store_model
            agent.writer.add_scalar("eval_reward", evalMeanReward, ep_it)
            agent.writer.add_scalar("eval_packet_loss_rate", np.mean(test_packet_loss_rate), ep_it)
            agent.writer.add_scalar("eval_total_delay", np.mean(test_total_delay), ep_it)
            agent.writer.add_scalar("eval_link_utilization", np.mean(test_link_utilization), ep_it)
            counter_store_model = counter_store_model + 1

        # fileLogs.flush()

        # Invoke garbage collection
        # tf.keras.backend.clear_session()
        gc.collect()

    for eps in tqdm(range(EVALUATION_EPISODES)):
        state, demand, source, destination = env_eval.reset()
        rewardAddTest = 0
        while 1:
            # We execute evaluation over current state
            # demand, src, dst
            action, _ = agent.act(env_eval, state, demand, source, destination, True)

            new_state, reward, done, demand, source, destination ,info= env_eval.make_step(state, action, demand, source,
                                                                                      destination)
            rewardAddTest = rewardAddTest + reward
            state = new_state
            if done:
                break
        rewards_test[eps] = rewardAddTest
    evalMeanReward = np.mean(rewards_test)

    if evalMeanReward > max_reward:
        max_reward = evalMeanReward
        reward_id = counter_store_model
    agent.writer.flush()
    agent.writer.close()
