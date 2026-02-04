#!/bin/bash

echo "API Container starting..."

# Wait for SSH keys to be available in shared volume
echo "Waiting for SSH keys to be available..."
max_attempts=30
attempt=0

while [ ! -f /ssh-shared/id_rsa ]; do
    attempt=$((attempt + 1))
    if [ $attempt -ge $max_attempts ]; then
        echo "ERROR: SSH keys not available after $max_attempts attempts"
        ls -la /ssh-shared/ 2>/dev/null || echo "Shared volume not accessible"
        exit 1
    fi
    echo "Attempt $attempt/$max_attempts: SSH keys not ready yet, waiting..."
    sleep 2
done

echo "SSH keys found! Copying to user directory..."

# Copy keys from shared volume to user directory
cp /ssh-shared/id_rsa /home/mpiuser/.ssh/id_rsa
cp /ssh-shared/id_rsa.pub /home/mpiuser/.ssh/id_rsa.pub
cp /ssh-shared/config /home/mpiuser/.ssh/config

# Set proper permissions
chown mpiuser:mpiuser /home/mpiuser/.ssh/*
chmod 700 /home/mpiuser/.ssh
chmod 600 /home/mpiuser/.ssh/id_rsa
chmod 644 /home/mpiuser/.ssh/id_rsa.pub
chmod 644 /home/mpiuser/.ssh/config

echo "SSH keys configured successfully!"
ls -la /home/mpiuser/.ssh/

# Wait for MPI master SSH to be ready
echo "Waiting for MPI master SSH daemon..."
max_attempts=30
attempt=0

while ! nc -z mpi-master 22 2>/dev/null; do
    attempt=$((attempt + 1))
    if [ $attempt -ge $max_attempts ]; then
        echo "WARNING: MPI master SSH not responding, but continuing..."
        break
    fi
    echo "Attempt $attempt/$max_attempts: Waiting for SSH on mpi-master..."
    sleep 2
done

echo "MPI master is ready!"

# Add MPI master host key to known_hosts
echo "Adding MPI master host key to known_hosts..."
ssh-keyscan -H mpi-master >> /home/mpiuser/.ssh/known_hosts 2>/dev/null
ssh-keyscan -H master >> /home/mpiuser/.ssh/known_hosts 2>/dev/null
chown mpiuser:mpiuser /home/mpiuser/.ssh/known_hosts
chmod 644 /home/mpiuser/.ssh/known_hosts

echo "Known hosts configured!"

# Start the API server
echo "Starting API server..."
exec uvicorn src.api:app --host 0.0.0.0 --port 8000
