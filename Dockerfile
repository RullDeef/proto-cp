# Dockerfile for example client
FROM debian:12.9
RUN apt update && apt upgrade -y && \
    apt install -y libavdevice-dev iproute2 iputils-ping
COPY docker-user-route.sh /app/route.sh
COPY build/selecon_cli /app/selecon
