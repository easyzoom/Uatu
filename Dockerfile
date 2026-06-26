# Stage 1: builder
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential cmake ninja-build pkg-config \
    libdw-dev libelf-dev libbpf-dev \
    clang llvm bpftool \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# Stage 2: runtime（精简镜像）
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y \
    libdw1 libelf1 libbpf0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/src/cli/uatu /usr/local/bin/uatu

ENTRYPOINT ["uatu"]
CMD ["--help"]
