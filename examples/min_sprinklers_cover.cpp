#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

struct Sprinkler {
  std::int64_t left = 0;
  std::int64_t right = 0;
};

int main() {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  int n = 0;
  int m = 0;
  if (!(std::cin >> n >> m)) {
    return 0;
  }

  std::vector<std::int64_t> trees(n);
  for (int i = 0; i < n; ++i) {
    std::cin >> trees[i];
  }

  std::vector<Sprinkler> sprinklers;
  sprinklers.reserve(m);
  for (int j = 0; j < m; ++j) {
    std::int64_t p = 0;
    std::int64_t r = 0;
    std::cin >> p >> r;
    sprinklers.push_back(Sprinkler {.left = p - r, .right = p + r});
  }

  if (n == 0) {
    std::cout << 0 << '\n';
    return 0;
  }

  std::sort(trees.begin(), trees.end());
  trees.erase(std::unique(trees.begin(), trees.end()), trees.end());

  std::sort(
    sprinklers.begin(),
    sprinklers.end(),
    [](const Sprinkler &a, const Sprinkler &b) {
      if (a.left != b.left) {
        return a.left < b.left;
      }
      return a.right > b.right;
    }
  );

  const int treeCount = static_cast<int>(trees.size());
  int i = 0;
  int j = 0;
  int used = 0;

  while (i < treeCount) {
    const std::int64_t need = trees[i];
    std::int64_t bestRight = need - 1;

    while (j < m && sprinklers[j].left <= need) {
      if (sprinklers[j].right > bestRight) {
        bestRight = sprinklers[j].right;
      }
      ++j;
    }

    if (bestRight < need) {
      std::cout << -1 << '\n';
      return 0;
    }

    ++used;
    while (i < treeCount && trees[i] <= bestRight) {
      ++i;
    }
  }

  std::cout << used << '\n';
  return 0;
}
