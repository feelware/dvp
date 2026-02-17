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
    librabbitmq-dev \
    libcurl4-openssl-dev \
    libcjson-dev \
    pkg-config \
    netcat-openbsd \
    curl \
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

RUN mkdir /var/log/mpi_jobs && chown mpiuser:mpiuser /var/log/mpi_jobs && chmod 755 /var/log/mpi_jobs

COPY src/rabbitmq_consumer.c /tmp/rabbitmq_consumer.c
RUN gcc -o /usr/local/bin/rabbitmq_consumer /tmp/rabbitmq_consumer.c -lrabbitmq -lcjson && \
    chmod +x /usr/local/bin/rabbitmq_consumer && \
    rm /tmp/rabbitmq_consumer.c

COPY src/main.cpp /tmp/main.cpp
COPY src/process_video.c /tmp/process_video.c
COPY src/video_decompose.h /tmp/video_decompose.h
COPY src/video_decompose.cpp /tmp/video_decompose.cpp

RUN cd /tmp && mpic++ -c video_decompose.cpp -o video_decompose.o $(pkg-config --cflags --libs opencv4)

RUN cd /tmp && mpic++ -Wall -std=c++11 -o main main.cpp $(pkg-config --cflags --libs opencv4) && \
    mv main /usr/local/bin/main && chmod +x /usr/local/bin/main

RUN cd /tmp && mpic++ -o process_video process_video.c video_decompose.o \
    -lcurl -lpthread $(pkg-config --cflags --libs opencv4) && \
    mv process_video /usr/local/bin/process_video && chmod +x /usr/local/bin/process_video

RUN rm -f /tmp/main.cpp /tmp/process_video.c /tmp/video_decompose.h /tmp/video_decompose.cpp /tmp/video_decompose.o

WORKDIR /home/mpiuser

CMD ["/bin/bash", "-c", "/usr/local/bin/init-ssh.sh && /usr/sbin/sshd -D"]