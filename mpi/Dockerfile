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
    dos2unix \
    && rm -rf /var/lib/apt/lists/*

RUN mkdir /var/run/sshd
RUN sed -i 's/UsePAM yes/UsePAM no/g' /etc/ssh/sshd_config
RUN sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin no/g' /etc/ssh/sshd_config

RUN useradd -m -s /bin/bash mpiuser && \
    echo "mpiuser:mpi" | chpasswd && \
    adduser mpiuser sudo && \
    echo "mpiuser ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

RUN mkdir -p /home/mpiuser/.ssh && \
    chown mpiuser:mpiuser /home/mpiuser/.ssh && \
    chmod 700 /home/mpiuser/.ssh

COPY init-ssh.sh /usr/local/bin/init-ssh.sh
RUN dos2unix /usr/local/bin/init-ssh.sh && chmod +x /usr/local/bin/init-ssh.sh

WORKDIR /home/mpiuser/project

CMD ["/usr/local/bin/init-ssh.sh"]
