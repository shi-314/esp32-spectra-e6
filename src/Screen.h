#ifndef SCREEN_H
#define SCREEN_H

class Screen {
 public:
  virtual ~Screen() = default;
  virtual void render() = 0;
  virtual int nextRefreshInSeconds() = 0;
};

#endif
