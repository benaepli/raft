# Lessons Learned

In this section, I've collected some of the bugs I've encountered while writing this library. I hope these are a bit
instructive for others.

## Bug Reports

### 2025-08-16: A Scary Initialization Race

- **File**: [2025-08-16-initialization-race.md](2025-08-16-initialization-race.md)
- **Summary**: A silent race condition caused by C++ initialization ordering results in a rare deadlock.