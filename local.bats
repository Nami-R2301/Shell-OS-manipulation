#!/usr/bin/env bats

load test_helper.bash

@test "Meme argument" {
	run ./pat -s -s -s -s
  [ "$status" = "0" ]
}

@test "Sous-shell" {
  run ./pat -s "+" sh -c ./prog3
  checki 143 <<-FIN
		+++ stdout
		killme
		+++ stderr
		Terminated
		+++ exit, status=143
		FIN
}

@test "Valgrind" {
  run ./pat valgrind --leak-check=full --error-exitcode=5 ./pat ./prog3 + ./prog1 + ./prog2
  [ "$status" != "5" ]
}

@test "Test argument 2" {
  run ./pat -s echo
  [ "$status" = "1" ]
}

@test "sleep" {
  run ./pat sleep 4
  checki 0 <<-FIN
		+++ exit, status=0
		FIN
}

@test "null" {
  run ./pat -s "\0" echo -e '\0' "\0"
  checki 0 <<-FIN
		\0\0\0 stdout

		\0\0\0 exit, status=0
		FIN
}

