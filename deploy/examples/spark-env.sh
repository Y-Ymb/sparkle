#!/usr/bin/env bash

# This file is sourced when running various Spark programs.
# Copy it as spark-env.sh and edit that to configure Spark for your site.

# Options read when launching programs locally with 
# ./bin/run-example or ./bin/spark-submit
# - HADOOP_CONF_DIR, to point Spark towards Hadoop configuration files
# - SPARK_LOCAL_IP, to set the IP address Spark binds to on this node
# - SPARK_PUBLIC_DNS, to set the public dns name of the driver program
# - SPARK_CLASSPATH, default classpath entries to append

# Options read by executors and drivers running inside the cluster
# - SPARK_LOCAL_IP, to set the IP address Spark binds to on this node
# - SPARK_PUBLIC_DNS, to set the public DNS name of the driver program
# - SPARK_CLASSPATH, default classpath entries to append
# - SPARK_LOCAL_DIRS, storage directories to use on this node for shuffle and RDD data
# - MESOS_NATIVE_LIBRARY, to point to your libmesos.so if you use Mesos

# Options read in YARN client mode
# - HADOOP_CONF_DIR, to point Spark towards Hadoop configuration files
# - SPARK_EXECUTOR_INSTANCES, Number of workers to start (Default: 2)
# - SPARK_EXECUTOR_CORES, Number of cores for the workers (Default: 1).
# - SPARK_EXECUTOR_MEMORY, Memory per Worker (e.g. 1000M, 2G) (Default: 1G)
# - SPARK_DRIVER_MEMORY, Memory for Master (e.g. 1000M, 2G) (Default: 512 Mb)
# - SPARK_YARN_APP_NAME, The name of your application (Default: Spark)
# - SPARK_YARN_QUEUE, The hadoop queue to use for allocation requests (Default: ‘default’)
# - SPARK_YARN_DIST_FILES, Comma separated list of files to be distributed with the job.
# - SPARK_YARN_DIST_ARCHIVES, Comma separated list of archives to be distributed with the job.

# Options for the daemons used in the standalone deploy mode:
# - SPARK_MASTER_IP, to bind the master to a different IP address or hostname
SPARK_MASTER_IP="smaug-1.u.hpl.hp.com"
# - SPARK_MASTER_PORT / SPARK_MASTER_WEBUI_PORT, to use non-default ports for the master
# - SPARK_MASTER_OPTS, to set config properties only for the master (e.g. "-Dx=y")
# - SPARK_WORKER_CORES, to set the number of cores to use on this machine
SPARK_WORKER_CORES=12
# - SPARK_WORKER_MEMORY, to set how much total memory workers have to give executors (e.g. 1000m, 2g)
SPARK_WORKER_MEMORY=96g
# - SPARK_WORKER_PORT / SPARK_WORKER_WEBUI_PORT, to use non-default ports for the worker
SPARK_WORKER_WEBUI_PORT=8285
# - SPARK_WORKER_INSTANCES, to set the number of worker processes per node
SPARK_WORKER_INSTANCES=1

# Or use the following to assign the number of processors as calculated when deploying the cluster
#SPARK_JAVA_OPTS="-XX:ParallelGCThreads=__NUM_PROCESSORS"
#SPARK_DAEMON_JAVA_OPTS=$SPARK_JAVA_OPTS
SPARK_DAEMON_JAVA_OPTS="-XX:ParallelGCThreads=10"

## for debugging/logging on shm shuffle engine
GLOG_minloglevel=1
GLOG_v=0
##specify RMB log level
rmb_log=error

# - SPARK_WORKER_DIR, to set the working directory of worker processes
SPARK_WORKER_DIR=/var/tmp/store-w20/worker
# - SPARK_LOCAL_DIRS, storage directories to use on this node for shuffle and RDD data
SPARK_LOCAL_DIRS=/var/tmp/store-w20/local
# - SPARK_WORKER_OPTS, to set config properties only for the worker (e.g. "-Dx=y")
# - SPARK_HISTORY_OPTS, to set config properties only for the history server (e.g. "-Dx=y")
# - SPARK_DAEMON_JAVA_OPTS, to set config properties for all daemons (e.g. "-Dx=y")
# - SPARK_PUBLIC_DNS, to set the public dns name of the master or workers

# - Multicore NUMA memory setting for this master/worker node
SPARK_MEMNODE_AFFINITY=4
# - Multicore NUMA CPU core setting for this master/worker node
SPARK_CORE_AFFINITY=300,301,302,303,304,305,306,307,308,309,310,311,312,313,314
# - Multicore NUMA CPU core setting for this master/worker node
SPARK_EXECUTOR_MEMNODE_AFFINITY=4
SPARK_EXECUTOR_CORE_AFFINITY=300,301,302,303,304,305,306,307,308,309,310,311,312,313,314
