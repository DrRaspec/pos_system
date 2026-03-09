FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
  build-essential \
  cmake \
  git \
  pkg-config \
  libpqxx-dev \
  libssl-dev \
  ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY backend /app/backend

WORKDIR /app/backend
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build --config Release --parallel

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
  libpqxx-dev \
  libssl3 \
  ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /app
RUN mkdir -p /app/uploads
COPY --from=build /app/backend/build/pos_backend /usr/local/bin/pos_backend

EXPOSE 8080
CMD ["pos_backend"]