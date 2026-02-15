#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

struct Query {
  int type = 0;
  int a = 0;
  int b = 0;
  int c = 0;
};

struct FenwickOfFenwick {
  int n = 0;
  std::vector<std::vector<int>> coords;
  std::vector<std::vector<int>> bit;

  explicit FenwickOfFenwick(int n_) : n(n_), coords(n_ + 1), bit(n_ + 1) {}

  static void bitAdd(std::vector<int> &tree, int pos, int delta) {
    const int sz = static_cast<int>(tree.size()) - 1;
    for (int i = pos; i <= sz; i += i & -i) {
      tree[i] += delta;
    }
  }

  static int bitSum(const std::vector<int> &tree, int pos) {
    int out = 0;
    for (int i = pos; i > 0; i -= i & -i) {
      out += tree[i];
    }
    return out;
  }

  // Register that position idx may contain value v at some time.
  void addCandidate(int idx, int v) {
    for (int i = idx; i <= n; i += i & -i) {
      coords[i].push_back(v);
    }
  }

  void build() {
    for (int i = 1; i <= n; ++i) {
      auto &cv = coords[i];
      std::sort(cv.begin(), cv.end());
      cv.erase(std::unique(cv.begin(), cv.end()), cv.end());
      bit[i].assign(cv.size() + 1, 0);
    }
  }

  void addPoint(int idx, int v, int delta) {
    for (int i = idx; i <= n; i += i & -i) {
      const auto &cv = coords[i];
      const int pos = static_cast<int>(std::lower_bound(cv.begin(), cv.end(), v) - cv.begin()) + 1;
      bitAdd(bit[i], pos, delta);
    }
  }

  // Count of values <= x in A[1..idx].
  int prefixCountLE(int idx, int x) const {
    int out = 0;
    for (int i = idx; i > 0; i -= i & -i) {
      const auto &cv = coords[i];
      const int pos = static_cast<int>(std::upper_bound(cv.begin(), cv.end(), x) - cv.begin());
      out += bitSum(bit[i], pos);
    }
    return out;
  }

  int rangeCountLE(int l, int r, int x) const {
    if (l > r) {
      return 0;
    }
    return prefixCountLE(r, x) - prefixCountLE(l - 1, x);
  }
};

int main() {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  int n = 0;
  int q = 0;
  std::cin >> n >> q;

  std::int64_t s = 0;
  std::int64_t p = 0;
  std::int64_t qcoef = 0;
  std::int64_t mod = 0;
  std::cin >> s >> p >> qcoef >> mod;

  std::vector<int> current(n + 1, 0);
  current[1] = static_cast<int>(s);
  for (int i = 2; i <= n; ++i) {
    const std::int64_t next = (static_cast<std::int64_t>(current[i - 1]) * p + qcoef) % mod;
    current[i] = static_cast<int>(next);
  }

  std::vector<Query> queries;
  queries.reserve(q);

  FenwickOfFenwick ds(n);
  std::vector<int> allValues;
  allValues.reserve(n + q);

  for (int i = 1; i <= n; ++i) {
    ds.addCandidate(i, current[i]);
    allValues.push_back(current[i]);
  }

  for (int i = 0; i < q; ++i) {
    Query qr;
    std::cin >> qr.type;
    if (qr.type == 1) {
      // 1 idx v
      std::cin >> qr.a >> qr.b;
      ds.addCandidate(qr.a, qr.b);
      allValues.push_back(qr.b);
    } else if (qr.type == 2 || qr.type == 3) {
      // 2 l r k  /  3 l r x
      std::cin >> qr.a >> qr.b >> qr.c;
    } else {
      // 4 idx
      std::cin >> qr.a;
    }
    queries.push_back(qr);
  }

  std::sort(allValues.begin(), allValues.end());
  allValues.erase(std::unique(allValues.begin(), allValues.end()), allValues.end());

  ds.build();
  for (int i = 1; i <= n; ++i) {
    ds.addPoint(i, current[i], +1);
  }

  std::string out;
  out.reserve(static_cast<size_t>(q) * 4);

  for (const Query &qr : queries) {
    if (qr.type == 1) {
      const int idx = qr.a;
      const int newVal = qr.b;
      ds.addPoint(idx, current[idx], -1);
      ds.addPoint(idx, newVal, +1);
      current[idx] = newVal;
      continue;
    }

    if (qr.type == 2) {
      const int l = qr.a;
      const int r = qr.b;
      const int k = qr.c;
      int lo = 0;
      int hi = static_cast<int>(allValues.size()) - 1;
      int ans = allValues[hi];
      while (lo <= hi) {
        const int mid = lo + ((hi - lo) >> 1);
        const int cnt = ds.rangeCountLE(l, r, allValues[mid]);
        if (cnt >= k) {
          ans = allValues[mid];
          hi = mid - 1;
        } else {
          lo = mid + 1;
        }
      }
      out += std::to_string(ans);
      out.push_back('\n');
      continue;
    }

    if (qr.type == 3) {
      const int l = qr.a;
      const int r = qr.b;
      const int x = qr.c;
      const int ans = ds.rangeCountLE(l, r, x);
      out += std::to_string(ans);
      out.push_back('\n');
      continue;
    }

    // type 4
    const int idx = qr.a;
    out += std::to_string(current[idx]);
    out.push_back('\n');
  }

  std::cout << out;
  return 0;
}

