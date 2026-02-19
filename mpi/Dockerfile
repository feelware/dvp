FROM nvidia/cuda:13.1.1-devel-ubuntu24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies
# Note: We use nvidia-cuda base, so we have nvcc.
# We need OpenMPI, OpenCV, and other utils.
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
    git \
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

# Compile RabbitMQ Consumer
COPY src/rabbitmq_consumer.c /tmp/rabbitmq_consumer.c
RUN gcc -o /usr/local/bin/rabbitmq_consumer /tmp/rabbitmq_consumer.c -lrabbitmq -lcjson && \
    chmod +x /usr/local/bin/rabbitmq_consumer && \
    rm /tmp/rabbitmq_consumer.c

# Copy Source Files
COPY src/process_video.c /tmp/process_video.c
COPY src/video_decompose.h /tmp/video_decompose.h
COPY src/video_decompose.cpp /tmp/video_decompose.cpp
COPY src/filters.h /tmp/filters.h
COPY src/filters_omp.cpp /tmp/filters_omp.cpp
COPY src/filters_cuda.cu /tmp/filters_cuda.cu

# Compile
WORKDIR /tmp

# 1. Compile CUDA filters
RUN nvcc -c filters_cuda.cu -o filters_cuda.o

# 2. Compile OpenMP filters
# Note: We need -fPIC for linking? Or just standard object.
RUN g++ -c filters_omp.cpp -o filters_omp.o -fopenmp -fPIC

# 3. Compile video_decompose (C++)
# Needs to see filters.h and link against OpenCV and OpenMP
RUN mpic++ -c video_decompose.cpp -o video_decompose.o -fopenmp $(pkg-config --cflags --libs opencv4)

# 4. Compile process_video (C/MPI) and Link everything
# We use mpic++ for the final link to handle C++ libs (OpenCV) and CUDA libs easily?
# Or use mpicc and explicitly link stdc++.
# Let's use mpic++ for final link.
# We need to link against CUDA runtime (-lcudart) and OpenMP (-fopenmp).
# CUDA lib path might need to be specified if not in standard path, but usually /usr/local/cuda/lib64 is in ldpath or we add -L...
RUN mpic++ -o process_video \
    process_video.c \
    video_decompose.o \
    filters_cuda.o \
    filters_omp.o \
    -lcurl -lpthread -fopenmp -L/usr/local/cuda/lib64 -lcudart \
    $(pkg-config --cflags --libs opencv4) && \
    mv process_video /usr/local/bin/process_video && \
    chmod +x /usr/local/bin/process_video

# Clean up
RUN rm -f /tmp/*.c /tmp/*.cpp /tmp/*.h /tmp/*.cu /tmp/*.o

WORKDIR /home/mpiuser

CMD ["/bin/bash", "-c", "/usr/local/bin/init-ssh.sh && /usr/sbin/sshd -D"]