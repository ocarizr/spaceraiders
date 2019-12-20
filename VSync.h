#ifndef VSINC_H
#define VSINC_H
class VSync
{
    int fps;
    const int thirty = 1000/30;
    const int sixty = 1000/60;

    void SetFps(int timestamp)
    {
        fps = timestamp < sixty ? 60 : 30;
    }

public:
    int GetFramerate() { return fps; }

    int GetFramePeriod()
    {
        return fps == 30 ? thirty : sixty;
    }

    int GetSyncPeriod(int timestamp)
    {
        SetFps(timestamp);
        return fps == 30 ? thirty - timestamp : sixty - timestamp;
    }
};

#endif // VSINC_H
