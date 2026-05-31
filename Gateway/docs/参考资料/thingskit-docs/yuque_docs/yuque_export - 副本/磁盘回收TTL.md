---
title: 磁盘回收TTL
---

:::info
💡 提示

执行DELETE语句后，<font style="color:rgb(64, 64, 64);"> 只是将数据在表中被标记为"已删除"并不能释放磁盘。磁盘释放需要执行VACUUM操作。</font>

:::

# 前置条件
1、开启TTL功能

2、配置TTL时间(平台配置、租户配置)

# 磁盘回收执行者
## 平台CORE服务
:::info
CORE服务启动后，根据执行频率延迟执行。

:::

### 磁盘回收原理
1、租户存在。

2、租户配置存在。

3、从租户配置获取配置的TTL信息。

4、从数据库获取过期数据。

5.1、执行数据库命令删除数据

5.2、执行数据库命令删除表中过期(平台配置)的分区释放磁盘。

```plain
public long dropPartitionsBefore(String table, long ts, long partitionDurationMs) {
        List<Long> partitions = fetchPartitions(table);
        long lastDroppedPartitionEndTime = -1;
        for (Long partitionStartTime : partitions) {
            long partitionEndTime = getPartitionEndTime(partitionStartTime, partitionDurationMs);
            if (partitionEndTime < ts) {
                log.info("[{}] Detaching expired partition: [{}-{}]", table, partitionStartTime, partitionEndTime);
                boolean success = detachAndDropPartition(table, partitionStartTime);
                if (success) {
                    log.info("[{}] Detached expired partition: {}", table, partitionStartTime);
                    lastDroppedPartitionEndTime = Math.max(partitionEndTime, lastDroppedPartitionEndTime);
                }
            } else {
                log.debug("[{}] Skipping valid partition: {}", table, partitionStartTime);
            }
        }
        return lastDroppedPartitionEndTime;
    }
```

```plain
private boolean detachAndDropPartition(String table, long partitionTs) {
        Map<Long, SqlPartition> cachedPartitions = tablesPartitions.get(table);
        if (cachedPartitions != null) cachedPartitions.remove(partitionTs);

        String tablePartition = table + "_" + partitionTs;
        String detachPsqlStmtStr = "ALTER TABLE " + table + " DETACH PARTITION " + tablePartition;

        // hotfix of ERROR: partition "integration_debug_event_1678323600000" already pending detach in partitioned table "public.integration_debug_event"
        // https://github.com/thingsboard/thingsboard/issues/8271
        // if (getCurrentServerVersion() >= PSQL_VERSION_14) {
        //    detachPsqlStmtStr += " CONCURRENTLY";
        // }

        String dropStmtStr = "DROP TABLE " + tablePartition;
        try {
            getJdbcTemplate().execute(detachPsqlStmtStr);
            getJdbcTemplate().execute(dropStmtStr);
            return true;
        } catch (DataAccessException e) {
            log.error("[{}] Error occurred trying to detach and drop the partition {} ", table, partitionTs, e);
        }
        return false;
    }
```

### 回收内容
AuditLogEntity(audit_log)

EdgeEventEntity(edge_event)

事件(error_event、lc_event、stats_event、rule_node_debug_event、rule_chain_debug_event、cf_debug_event)

NotificationRequestEntity(notification_request)

遥测数据(ts_kv)

## 时序数据库TimescaleDb
### 磁盘回收原理
1、基于TTL时间为timescaledb创建磁盘回收策略，。

2、timescale的回收策略定期释放磁盘。

### 回收内容
遥测数据(ts_kv)

## 分布式数据库Cassandra
### 磁盘回收原理
1、数据入库时设置数据TTL时间。

2、cassandra定期释放磁盘。

### 回收内容
遥测数据(ts_kv)
