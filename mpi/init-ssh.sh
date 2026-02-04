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

# Start SSH daemon
echo "Starting SSH daemon..."
sudo /usr/sbin/sshd -D
