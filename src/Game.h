#ifndef GAME_H
#define GAME_H

#include <vector>

class Game
{
	public:
		static constexpr unsigned int WIDTH = 9;
		static constexpr unsigned int HEIGHT = 9;
		static constexpr unsigned int BLOCK_WIDTH = WIDTH / 3;
		static constexpr unsigned int BLOCK_HEIGHT = HEIGHT / 3;
		static constexpr unsigned char MAX_VALUE = 9;
		static constexpr unsigned char EMPTY_VALUE = 0;

		Game();

		void Clear();
		bool Set(const unsigned int x,const unsigned int y,const unsigned char value);
		unsigned char Get(const unsigned int x,const unsigned int y) const;
		void Print();
	private:
		std::vector<unsigned char> state;
};

#endif

