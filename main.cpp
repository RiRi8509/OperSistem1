#include <pthread.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

// Некопируемый объект изображает несериализуемые данные события.
class Event {
public:
    explicit Event(int number)
        : number_(number), data_(std::make_unique<int[]>(4)) {
        for (int i = 0; i < 4; ++i) {
            data_[i] = number * 10 + i;
        }
    }

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    int number() const {
        return number_;
    }

    int checksum() const {
        return data_[0] + data_[1] + data_[2] + data_[3];
    }

private:
    int number_;
    std::unique_ptr<int[]> data_;
};

class Monitor {
public:
    ~Monitor() {
        pthread_cond_destroy(&condition_);
        pthread_mutex_destroy(&mutex_);
    }

    void provide(std::unique_ptr<Event> event) {
        pthread_mutex_lock(&mutex_);

        // Не перезаписываем ещё не обработанное событие.
        while (ready_) {
            pthread_cond_wait(&condition_, &mutex_);
        }

        event_ = std::move(event);
        ready_ = true;

        std::cout << "[Поставщик]  Отправлено событие #"
                  << event_->number() << std::endl;

        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
    }

    bool consume() {
        pthread_mutex_lock(&mutex_);

        // while защищает от ложных пробуждений.
        while (!ready_ && !stopping_) {
            // Во время ожидания функция сама временно освобождает mutex.
            pthread_cond_wait(&condition_, &mutex_);
        }

        if (!ready_ && stopping_) {
            pthread_mutex_unlock(&mutex_);
            return false;
        }

        ready_ = false;

        std::cout << "[Потребитель] Обработано событие #"
                  << event_->number()
                  << ", контрольная сумма = " << event_->checksum()
                  << std::endl;

        event_.reset();

        // Поставщик мог ждать освобождения места для нового события.
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
        return true;
    }

    void stop() {
        pthread_mutex_lock(&mutex_);
        stopping_ = true;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
    }

private:
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t condition_ = PTHREAD_COND_INITIALIZER;
    bool ready_ = false;
    bool stopping_ = false;
    std::unique_ptr<Event> event_;
};

struct ThreadData {
    Monitor* monitor;
    int event_count;
};

void* producer(void* argument) {
    auto* data = static_cast<ThreadData*>(argument);

    for (int number = 1; number <= data->event_count; ++number) {
        sleep(1);  // Задержка нужна только для имитации появления события.
        data->monitor->provide(std::make_unique<Event>(number));
    }

    data->monitor->stop();
    return nullptr;
}

void* consumer(void* argument) {
    auto* data = static_cast<ThreadData*>(argument);

    while (data->monitor->consume()) {
    }

    return nullptr;
}

int main(int argc, char* argv[]) {
    int event_count = 10;

    try {
        if (argc == 2) {
            event_count = std::stoi(argv[1]);
        }
        if (argc > 2 || event_count <= 0) {
            throw std::invalid_argument("неверное число событий");
        }
    } catch (...) {
        std::cerr << "Использование: ./monitor_lab [положительное число]\n";
        return EXIT_FAILURE;
    }

    Monitor monitor;
    ThreadData data{&monitor, event_count};
    pthread_t producer_thread;
    pthread_t consumer_thread;

    if (pthread_create(&consumer_thread, nullptr, consumer, &data) != 0) {
        std::cerr << "Не удалось создать поток-потребитель\n";
        return EXIT_FAILURE;
    }

    if (pthread_create(&producer_thread, nullptr, producer, &data) != 0) {
        monitor.stop();
        pthread_join(consumer_thread, nullptr);
        std::cerr << "Не удалось создать поток-поставщик\n";
        return EXIT_FAILURE;
    }

    pthread_join(producer_thread, nullptr);
    pthread_join(consumer_thread, nullptr);

    std::cout << "Работа завершена: обработано событий — "
              << event_count << std::endl;
    return EXIT_SUCCESS;
}
