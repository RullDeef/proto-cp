# Dockerfile for example client
FROM alpine:3.20.1
RUN apk add libc6-compat
COPY build/example /app/selecon
CMD [ "/app/selecon" ]
