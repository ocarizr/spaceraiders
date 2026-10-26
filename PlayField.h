#ifndef PLAYFIELD_H
#define PLAYFIELD_H
#include <atomic>
#include <list>
#include <memory>
#include <ranges>
#include <unordered_set>

#include "GameObject.h"
#include "Input.h"
#include "VSync.h"

class GameMode;

class PlayField
{
  private:
    static constexpr int InitialAlienLaserSlots = 10;
    static constexpr int InitialPlayerLaserSlots = 5;
    static constexpr int AlienLaserSlotsPerAlien = 3;

    std::list<std::unique_ptr<GameObject>> gameObjects;
    std::list<std::unique_ptr<GameObject>> gameObjectsToAdd;
    std::unordered_set<GameObjPtr> gameObjectsToRemove;
    std::unique_ptr<GameMode> gameMode;

    bool OwnsObject(GameObjPtr object) const;

  public:
    std::unique_ptr<Input> controllerInput;
    Vector2D bounds;
    VSync vsync;

    // Available laser slots; gameplay budgeting is refined by A10.
    std::atomic<int> AlienLasers;
    std::atomic<int> PlayerLasers;

    explicit PlayField(const Vector2D& iBounds);
    ~PlayField();
    PlayField(const PlayField&) = delete;

    PlayField& operator=(const PlayField&) = delete;

    PlayField(PlayField&&) = delete;

    PlayField& operator=(PlayField&&) = delete;

    // Borrowed objects remain alive until ProcessQueues/RemoveAllObjects.
    // Pending removals disappear immediately from queries and collision traversal.
    auto GameObjects() const
    {
        return gameObjects |
               std::views::filter(
                   [this](const auto& object)
                   {
                       return !gameObjectsToRemove.contains(object.get());
                   }) |
               std::views::transform(
                   [](const auto& object)
                   {
                       return object.get();
                   });
    }

    void Update();
    GameObjPtr GetPlayerObject();
    bool HasPlayer();
    bool HasAliens();
    int AlienLaserCount();
    int AlienCount();
    void SpawnLaser(GameObjPtr newObj);
    void DespawnLaser(GameObjPtr newObj);

    // Transfers ownership on first registration; null/duplicate registrations
    // are ignored. The caller must not delete or transfer an owned object elsewhere.
    bool AddObject(GameObjPtr newObj);
    bool RemoveObject(GameObjPtr object);
    void RemoveAllObjects();
    void ProcessQueues();

    [[nodiscard]] GameMode* GetGameMode() const;
    // Transfers ownership, replacing the old mode. Passing the current mode is a no-op.
    void SetGameMode(GameMode* value);

    void AddAlienWave(Vector2D& canvasSize);
    void AddBlockWalls(Vector2D& canvasSize);
    void AddPlayer(Vector2D& canvasSize);
};

#endif // PLAYFIELD_H
