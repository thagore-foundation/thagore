def main() -> None:
    n = 200000
    acc = 0
    i = 0
    while i < n:
        acc += i * i
        i += 1
    print(acc)


if __name__ == "__main__":
    main()
