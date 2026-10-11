# syntax=docker/dockerfile:1

FROM debian:trixie AS builder

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libboost-dev \
    libboost-log-dev \
    libpqxx-dev \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/src/chouchou
COPY CMakeLists.txt ./
COPY src ./src

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --target chouchou --parallel "$(nproc)"

FROM debian:trixie AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends libpq5 libpqxx-7.10 libboost-log1.83.0 curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 chouchou \
    && mkdir -p /var/log/chouchou \
    && chown chouchou:chouchou /var/log/chouchou

ENV LOG_DIR=/var/log/chouchou

COPY --from=builder /usr/local/src/chouchou/build/chouchou /usr/local/bin/chouchou

USER chouchou

EXPOSE 3000

ENTRYPOINT ["/usr/local/bin/chouchou"]
