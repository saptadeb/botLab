FROM ubuntu:18.04

# Install Botlab apt software
RUN apt update && \
  apt install -y \
  wget \
  unzip \
  make \
  cmake \
  g++ \
  openjdk-8-jdk \
  libglib2.0-dev \
  libgtk2.0-dev \
  libdc1394-22-dev \
  libusb-1.0-0-dev \
  libgsl-dev \
  freeglut3-dev \
  libboost-dev \
  python3-pip \
  && \
  rm -rf /var/lib/apt/lists/*

# Install python software
RUN pip3 install \
  pygame \
  numpy

# Install lcm
RUN cd /tmp && \
  wget https://github.com/lcm-proj/lcm/releases/download/v1.4.0/lcm-1.4.0.zip && \
  unzip -qq lcm-1.4.0.zip && \
  cd lcm-1.4.0 && \
  mkdir build && \
  cd build && \
  cmake .. && \
  make && \
  make install && \
  ldconfig && \
  cd ../lcm-python && \
  python3 setup.py install && \
  cd /tmp && \
  rm -rf lcm*

# Start Debug tools
# Add more temporary installs here:
RUN apt update && \
  apt install -y \
  sudo && \
  rm -rf /var/lib/apt/lists/*

# Temporary user credintials
RUN useradd -ms /bin/bash user -G video && \
  echo "user:i<3robots!" | chpasswd && \
  adduser user sudo

WORKDIR /home/user/botlab

USER user
