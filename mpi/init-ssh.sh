#!/bin/bash

echo "Initializing SSH for MPI node..."

# Copy keys from shared volume to user directory
if [ -f /ssh-shared/id_rsa ]; then
    echo "Copying SSH keys from shared volume..."
    cp /ssh-shared/id_rsa /home/mpiuser/.ssh/id_rsa
    cp /ssh-shared/id_rsa.pub /home/mpiuser/.ssh/id_rsa.pub
    cp /ssh-shared/authorized_keys /home/mpiuser/.ssh/authorized_keys
    cp /ssh-shared/config /home/mpiuser/.ssh/config

    # Set correct ownership and permissions
    chown mpiuser:mpiuser /home/mpiuser/.ssh/*
    chmod 700 /home/mpiuser/.ssh
    chmod 600 /home/mpiuser/.ssh/id_rsa
    chmod 644 /home/mpiuser/.ssh/id_rsa.pub
    chmod 644 /home/mpiuser/.ssh/authorized_keys
    chmod 644 /home/mpiuser/.ssh/config

    echo "SSH keys copied and configured successfully."
else
    echo "ERROR: SSH keys not found in shared volume!"
    exit 1
fi

# Esperar a que RabbitMQ esté disponible SOLO en el master
HOSTNAME=$(hostname)
if [ "$HOSTNAME" = "master" ] || [ "$HOSTNAME" = "mpi-master" ]; then
    echo "Waiting for RabbitMQ to be ready..."
    for i in {1..30}; do
        if nc -z rabbitmq 5672 2>/dev/null; then
            echo "RabbitMQ is ready!"
            break
        fi
        echo "Attempt $i/30: RabbitMQ not ready yet, waiting..."
        sleep 2
    done

    # Iniciar el consumer de RabbitMQ en background (SOLO EN MASTER)
    echo "Starting RabbitMQ consumer in background..."
    /usr/local/bin/rabbitmq_consumer > /var/log/rabbitmq_consumer.log 2>&1 &
    CONSUMER_PID=$!
    echo "RabbitMQ consumer started with PID: $CONSUMER_PID"
    echo "Logs: tail -f /var/log/rabbitmq_consumer.log"
else
    echo "This is a worker node ($HOSTNAME), skipping RabbitMQ consumer..."
fi

# Start SSH daemon
echo "Starting SSH daemon..."
sudo /usr/sbin/sshd -D
