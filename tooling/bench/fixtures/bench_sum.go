package main

import "fmt"

func main() {
	var n int64 = 200000
	var acc int64 = 0
	var i int64 = 0
	for i < n {
		acc += i * i
		i++
	}
	fmt.Println(acc)
}
