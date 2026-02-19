#!/bin/bash

echo "Initializing SSH for MPI node..."

# Copy keys from shared volume to user directory
if [ -f /ssh-shared/id_rsa ]; then
    echo "Copying SSH keys from shared volume..."
    
    # Use install command which handles permissions better
    install -m 600 -o mpiuser -g mpiuser /ssh-shared/id_rsa /home/mpiuser/.ssh/id_rsa
    install -m 644 -o mpiuser -g mpiuser /ssh-shared/id_rsa.pub /home/mpiuser/.ssh/id_rsa.pub
    install -m 644 -o mpiuser -g mpiuser /ssh-shared/authorized_keys /home/mpiuser/.ssh/authorized_keys
    install -m 644 -o mpiuser -g mpiuser /ssh-shared/config /home/mpiuser/.ssh/config

    echo "SSH keys copied and configured successfully."
    
    # Verify that id_rsa was copied
    if [ ! -f /home/mpiuser/.ssh/id_rsa ]; then
        echo "ERROR: id_rsa was not copied successfully!"
        ls -la /ssh-shared/
        ls -la /home/mpiuser/.ssh/
        exit 1
    fi
    echo "Verified: id_rsa exists with correct permissions"
else
    echo "ERROR: SSH keys not found in shared volume!"
    exit 1
fi

# Ensure the log file exists and has the correct permissions
if [ ! -f /var/log/rabbitmq_consumer.log ]; then
    echo "Creating RabbitMQ consumer log file..."
    touch /var/log/rabbitmq_consumer.log
    chown mpiuser:mpiuser /var/log/rabbitmq_consumer.log
    chmod 644 /var/log/rabbitmq_consumer.log
fi

# Ensure the MPI jobs log directory exists
if [ ! -d /var/log/mpi_jobs ]; then
    echo "Creating MPI jobs log directory..."
    mkdir -p /var/log/mpi_jobs
    chown mpiuser:mpiuser /var/log/mpi_jobs
    chmod 755 /var/log/mpi_jobs
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

    # Iniciar el consumer de RabbitMQ en FOREGROUND (SOLO EN MASTER)
    # Primero necesitamos iniciar sshd para que mpirun funcione localmente
    echo "Starting sshd locally for MPI..."
    /usr/sbin/sshd
    
    echo "Starting RabbitMQ consumer in foreground..."
    # Ejecutamos el consumer y enviamos logs a archivo y stdout
    /usr/local/bin/rabbitmq_consumer 2>&1 | tee /var/log/rabbitmq_consumer.log
    
    # Si el consumer termina (crash o exit), el script termina.
    # El CMD del Dockerfile intentará ejecutar sshd -D, que fallará porque el puerto 22 ya está en uso.
    # Esto causará que el contenedor se detenga, lo cual es correcto si el proceso principal muere.
else
    echo "This is a worker node ($HOSTNAME), skipping RabbitMQ consumer..."
fi

