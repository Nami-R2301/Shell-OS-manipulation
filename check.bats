#!/usr/bin/env bats

load test_helper

@test "base prog1" {
	run ./pat ./prog1
	checki 0 <<-FIN
		+++ stdout
		Bonjour
		+++ stderr
		Message d'erreur
		+++ stdout
		Le
		Monde
		+++ stderr
		Message d'erreur de fin
		+++ exit, status=0
		FIN
}

@test "base cat prog1" {
	run ./pat cat prog1
	checki 0 <<-FIN
		+++ stdout
		#!/bin/sh
		echo "Bonjour"
		sleep .2
		echo "Message d'erreur" >&2
		sleep .2
		echo "Le"
		sleep .2
		echo "Monde"
		sleep .2
		echo "Message d'erreur de fin" >&2
		+++ exit, status=0
		FIN
}

@test "base prog2" {
        run ./pat ./prog2
	checki 1 <<-FIN
		+++ stdout
		Hello
		++++ stderr
		First error
		++++ stdout
		World2nd World
		++++ stderr
		Last error
		++++ exit, status=1
		FIN
}

@test "base prog3" {
	run ./pat ./prog3
	checki 143 <<-FIN
		+++ stdout
		killme
		+++ exit, signal=15
		FIN
}

@test "base fail" {
	run ./pat fail
	checki 127 <<-FIN
		+++ stderr
		fail: No such file or directory
		+++ exit, status=127
		FIN
}

@test "base prog2 + prog1" {
	run ./pat ./prog2 + ./prog1
        checki 1 <<-FIN
		+++ stdout 2
		Bonjour
		+++ stdout 1
		Hello
		++++ stderr 2
		Message d'erreur
		+++ stderr 1
		First error
		++++ stdout 2
		Le
		+++ stdout 1
		World
		++++ stdout 2
		Monde
		+++ stdout 1
		2nd World
		++++ stderr 2
		Message d'erreur de fin
		+++ exit 2, status=0
		+++ stderr 1
		Last error
		++++ exit 1, status=1
		FIN

}

@test "base prog1 + prog2 + prog3" {
	run ./pat ./prog1 + ./prog2 + ./prog3
	checki 144 <<-FIN
		+++ stdout 1
		Bonjour
		+++ stdout 2
		Hello
		++++ stderr 1
		Message d'erreur
		+++ stderr 2
		First error
		++++ stdout 3
		killme
		+++ exit 3, signal=15
		+++ stdout 1
		Le
		+++ stdout 2
		World
		++++ stdout 1
		Monde
		+++ stdout 2
		2nd World
		++++ stderr 1
		Message d'erreur de fin
		+++ exit 1, status=0
		+++ stderr 2
		Last error
		++++ exit 2, status=1
		FIN
}

@test "base -s @@ ./prog2" {
	run ./pat -s @@ ./prog2
	checki 1 <<-FIN
		@@@@@@ stdout
		Hello
		@@@@@@@@ stderr
		First error
		@@@@@@@@ stdout
		World2nd World
		@@@@@@@@ stderr
		Last error
		@@@@@@@@ exit, status=1
		FIN
}

@test "base -s lo prog1 lo prog2 lo prog3" {
	run ./pat -s lo ./prog1 lo ./prog2 lo ./prog3
	checki 144 <<-FIN
		lololo stdout 1
		Bonjour
		lololo stdout 2
		Hello
		lolololo stderr 1
		Message d'erreur
		lololo stderr 2
		First error
		lolololo stdout 3
		killme
		lololo exit 3, signal=15
		lololo stdout 1
		Le
		lololo stdout 2
		World
		lolololo stdout 1
		Monde
		lololo stdout 2
		2nd World
		lolololo stderr 1
		Message d'erreur de fin
		lololo exit 1, status=0
		lololo stderr 2
		Last error
		lolololo exit 2, status=1
		FIN
}

@test "base pat -s @1 pat -s @2 ./prog1 @2 ./prog2 @1 ./prog3" {
	run ./pat -s @1 ./pat -s @2 ./prog1 @2 ./prog2 @1 ./prog3
	checki 144 <<-FIN
		@1@1@1 stdout 1
		@2@2@2 stdout 1
		Bonjour
		@2@2@2 stdout 2
		Hello
		@2@2@2@2 stderr 1
		Message d'erreur
		@2@2@2 stderr 2
		First error
		@1@1@1@1 stdout 2
		killme
		@1@1@1 exit 2, signal=15
		@1@1@1 stdout 1

		@2@2@2@2 stdout 1
		Le
		@2@2@2 stdout 2
		World
		@2@2@2@2 stdout 1
		Monde
		@2@2@2 stdout 2
		2nd World
		@2@2@2@2 stderr 1
		Message d'erreur de fin
		@2@2@2 exit 1, status=0
		@2@2@2 stderr 2
		Last error
		@2@2@2@2 exit 2, status=1
		@1@1@1 exit 1, status=1
		FIN
}
