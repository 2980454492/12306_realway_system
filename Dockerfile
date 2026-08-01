# 12306 铁路票务系统 — Docker 多阶段构建
# 阶段 1：编译 C++ 源码
FROM gcc:13-bookworm AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake libsodium-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /build
COPY server/ server/
RUN cmake -S server -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# 阶段 2：最小运行镜像
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    libsodium23 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /build/build/railway_server /app/
COPY server/config/ /app/config/
COPY server/frontend/ /app/frontend/
RUN mkdir -p /app/data
EXPOSE 8080
CMD ["./railway_server"]
