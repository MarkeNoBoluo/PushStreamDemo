#ifndef DATASTRUCT_H
#define DATASTRUCT_H
#include <QObject>
enum class PushState {
    none = 0,
    decode,
    play,
    pause,
    error,
    end
};
Q_DECLARE_METATYPE(PushState); // 在类的声明之后添加这个宏

#endif // DATASTRUCT_H
