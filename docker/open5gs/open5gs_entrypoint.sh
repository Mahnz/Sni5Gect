#! /bin/bash

export UE_GATEWAY_IP="${UE_IP_BASE}.1"
export UE_IP_RANGE="${UE_IP_BASE}.0/24"

INSTALL_ARCH=x86_64-linux-gnu
if [ "$(uname -m)" = "aarch64" ]; then
    INSTALL_ARCH="aarch64-linux-gnu"
fi
export INSTALL_ARCH

###############################################
# Resolve configuration file to be used by 5gc
###############################################
# Priority:
# 1) If /config/open5gs-5gc.yml.in exists (mounted template), render with envsubst -> /open5gs/open5gs-5gc.yml
# 2) Else if /config/open5gs-5gc.yml exists (mounted ready file), use as-is
# 3) Else render local template open5gs-5gc.yml.in -> open5gs-5gc.yml

MOUNTED_TEMPLATE="/config/open5gs-5gc.yml.in"
MOUNTED_READY="/config/open5gs-5gc.yml"
LOCAL_TEMPLATE_IN="open5gs-5gc.yml.in"
LOCAL_RENDERED_OUT="open5gs-5gc.yml"

if [ -f "${MOUNTED_TEMPLATE}" ]; then
    echo "[open5gs] Rendering mounted template: ${MOUNTED_TEMPLATE} -> /open5gs/${LOCAL_RENDERED_OUT}"
    envsubst < "${MOUNTED_TEMPLATE}" > "${LOCAL_RENDERED_OUT}"
    OPEN5GS_RENDERED_CFG="/open5gs/${LOCAL_RENDERED_OUT}"
elif [ -f "${MOUNTED_READY}" ]; then
    echo "[open5gs] Using mounted config: ${MOUNTED_READY}"
    OPEN5GS_RENDERED_CFG="${MOUNTED_READY}"
else
    echo "[open5gs] Mounted config not found. Rendering local template: ${LOCAL_TEMPLATE_IN} -> ${LOCAL_RENDERED_OUT}"
    envsubst < "${LOCAL_TEMPLATE_IN}" > "${LOCAL_RENDERED_OUT}"
    OPEN5GS_RENDERED_CFG="/open5gs/${LOCAL_RENDERED_OUT}"
fi

# Ensure log directory exists if referenced by config
mkdir -p /open5gs/logs || true

# create dummy interfaces on localhost ip range for open5gs entities to bind to
for IP in {2..22}
do
    ip link add name lo$IP type dummy
    ip ad ad 127.0.0.$IP/24 dev lo$IP
    ip link set lo$IP up
done

# run webui
cd webui && npm run dev &

# run mongodb
mkdir -p /data/db && mongod --logpath /tmp/mongodb.log &

# wait for mongodb to be available, otherwise open5gs will not start correctly
while ! ( nc -zv $MONGODB_IP 27017 2>&1 >/dev/null )
do
    echo waiting for mongodb
    sleep 1
done

# setup ogstun and routing
python3 setup_tun.py --ip_range ${UE_IP_RANGE}
if [ $? -ne 0 ]
then
    echo "Failed to setup ogstun and routing"
    exit 1
fi

# Add subscriber data to open5gs mongo db
echo "SUBSCRIBER_DB=${SUBSCRIBER_DB}"
python3 add_users.py --mongodb ${MONGODB_IP} --subscriber_data ${SUBSCRIBER_DB}
if [ $? -ne 0 ]
then
    echo "Failed to add subscribers to database"
    exit 1
fi

# Run 5gc with selected configuration
if [ "${DEBUG}" = "true" ]; then
    exec stdbuf -o L gdb -batch -ex=run -ex=bt --args 5gc -c "${OPEN5GS_RENDERED_CFG}"
else
    exec stdbuf -o L 5gc -c "${OPEN5GS_RENDERED_CFG}"
fi
