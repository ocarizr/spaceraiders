#ifndef PLAYFIELD_H
#define PLAYFIELD_H
#include <atomic>
#include <list>
#include <memory>
#include <ranges>
#include <unordered_set>
#include <vector>

#include "GameObject.h"
#include "Input.h"
#include "VSync.h"

class GameMode;

class PlayField
{
  private:
    static constexpr int InitialAlienLaserSlots = 10;
    static constexpr int InitialPlayerLaserSlots = 5;
    enum class LaserOwner
    {
        None,
        Player,
        Alien
    };
    int playerLaserCapacity = InitialPlayerLaserSlots;
    int alienLaserCapacity = InitialAlienLaserSlots;
    std::atomic<int> availablePlayerLaserSlots{InitialPlayerLaserSlots};
    std::atomic<int> availableAlienLaserSlots{InitialAlienLaserSlots};

    std::list<std::unique_ptr<GameObject>> gameObjects;
    std::list<std::unique_ptr<GameObject>> gameObjectsToAdd;
    std::unordered_set<GameObjPtr> gameObjectsToRemove;
    std::unique_ptr<GameMode> gameMode;

    bool OwnsObject(GameObjPtr object) const;
    static LaserOwner GetLaserOwner(GameObjPtr object);
    int AvailableLaserSlots(LaserOwner owner) const;
    void ReconcileLaserSlots(LaserOwner owner);

  public:
    std::unique_ptr<Input> controllerInput;
    Vector2D bounds;
    VSync vsync;

    static constexpr float LaserSpeedCellsPerSecond = 4.f;
    // Legacy read access to available slots. Configure limits through the setters.
    // World mutation remains single-threaded; atomic snapshots do not synchronize admission.
    const std::atomic<int>& AlienLasers = availableAlienLaserSlots;
    const std::atomic<int>& PlayerLasers = availablePlayerLaserSlots;

    explicit PlayField(const Vector2D& iBounds);
    ~PlayField();
    PlayField(const PlayField&) = delete;

    PlayField& operator=(const PlayField&) = delete;

    PlayField(PlayField&&) = delete;

    PlayField& operator=(PlayField&&) = delete;

    // Borrowed objects remain alive until ProcessQueues/RemoveAllObjects or Update entry.
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
    // Consumes new lasers even when rejected; already-owned duplicates are ignored.
    bool SpawnLaser(GameObjPtr newObj);
    // Consumes the batch on success/failure; only player lasers are accepted.
    bool SpawnPlayerLasers(std::vector<std::unique_ptr<GameObject>> lasers);
    void DespawnLaser(GameObjPtr newObj);
    bool ResolveLaserCollision(GameObjPtr laser);
    bool IsLiveObject(GameObjPtr object) const;

    int GetPlayerLaserCapacity() const noexcept
    {
        return playerLaserCapacity;
    }

    void SetPlayerLaserCapacity(int capacity);
    void SetAlienLaserCapacity(int capacity);

    // Consumes new objects, including rejected lasers. Null/owned duplicates are
    // ignored. The caller must not delete or transfer an owned object elsewhere.
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
