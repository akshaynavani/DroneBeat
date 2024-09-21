# ---- build stage -----------------------------------------------------------
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates pkg-config \
        libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc \
        libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY proto ./proto
COPY src ./src
COPY tests ./tests

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDRONEBEAT_BUILD_TESTS=OFF \
    && cmake --build build -j"$(nproc)" --target dronebeat_server drone_sim

# ---- runtime stage ---------------------------------------------------------
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgrpc++1.51t64 libprotobuf32t64 libsqlite3-0 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home dronebeat \
    && mkdir -p /data && chown dronebeat /data

COPY --from=build /src/build/src/dronebeat_server /usr/local/bin/
COPY --from=build /src/build/src/drone_sim /usr/local/bin/

USER dronebeat
ENV DRONEBEAT_DB_PATH=/data/dronebeat.db
EXPOSE 50051 8080
VOLUME ["/data"]
ENTRYPOINT ["dronebeat_server"]
