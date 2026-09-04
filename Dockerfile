FROM gcc:13 AS build
WORKDIR /src
COPY . .
RUN make CXX=g++

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/shardgate /app/shardgate
ENV SHARDGATE_PORT=8000 \
    SHARDGATE_SHARDS=/app/data
EXPOSE 8000
ENTRYPOINT ["/app/shardgate"]
