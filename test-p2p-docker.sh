#/bin/env sh

function user_ip() {
  echo "192.168.200.$1"
}

# test peer to peer conference using docker containers
docker compose build

docker compose down

docker compose run user1 --invite ${user_ip 2} &
docker compose run user2 --wait-invite &

