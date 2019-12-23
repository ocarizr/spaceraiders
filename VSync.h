#ifndef VSINC_H
#define VSINC_H
class VSync
{
    int fps;
    const int five = 1000/5;
    const int ten = 1000/10;
    const int twenty = 1000/20;
    const int thirty = 1000/30;
    const int sixty = 1000/60;

    void SetFps(const int& timestamp)
    {
        fps = timestamp < five ? 5 :
                               (timestamp < ten ? 10 :
                                                (timestamp < twenty ? 20 :
                                                                    (timestamp < thirty ? 30 :
                                                                                        60)));
    }

public:
    VSync() : fps(0) {}
    int GetFramerate() { return fps; }

    int GetFramePeriod()
    {
        switch (fps) {
        case 5: return five;
        case 10: return ten;
        case 20: return twenty;
        case 30: return thirty;
        default: return sixty;
        }
    }

    float DeltaTime()
    {
        return static_cast<float>(GetFramePeriod()) / 1000.f;
    }

    int GetSyncPeriod(const int& timestamp)
    {
        SetFps(timestamp);
        auto frame = GetFramePeriod();
        return frame - timestamp;
    }
};

#endif // VSINC_H
