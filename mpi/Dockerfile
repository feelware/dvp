FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    sudo \
    nano \
    openssh-server \
    openmpi-bin \
    libopenmpi-dev \
    libopencv-dev \
    ffmpeg \
    && rm -rf /var/lib/apt/lists/*
    
RUN mkdir /var/run/sshd
RUN sed -i 's/UsePAM yes/UsePAM no/g' /etc/ssh/sshd_config

RUN useradd -m -s /bin/bash mpiuser && \
    echo "mpiuser:mpi" | chpasswd && \
    adduser mpiuser sudo

USER mpiuser
RUN mkdir -p /home/mpiuser/.ssh && \
    ssh-keygen -t rsa -f /home/mpiuser/.ssh/id_rsa -q -N "" && \
    cp /home/mpiuser/.ssh/id_rsa.pub /home/mpiuser/.ssh/authorized_keys && \
    echo "StrictHostKeyChecking no" > /home/mpiuser/.ssh/config

USER root
WORKDIR /home/mpiuser/project
CMD ["/usr/sbin/sshd", "-D"]
