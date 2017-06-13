// Copyright 2017 James Bendig. See the COPYRIGHT file at the top-level
// directory of this distribution.
//
// Licensed under:
//   the MIT license
//     <LICENSE-MIT or https://opensource.org/licenses/MIT>
//   or the Apache License, Version 2.0
//     <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>,
// at your option. This file may not be copied, modified, or distributed
// except according to those terms.

#include "PuzzleFinder.h"
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <intrin.h>
#endif
#include "Image.h"

//An angle must be +/- this delta to be considered similar.
static constexpr float DELTA_THETA = M_PI / 12.0f;

static void FindLines(const unsigned int targetWidth,
					  const unsigned int targetHeight,
					  const Image& houghTransformFrame,
					  std::vector<Line>& lines)
{
	//Find peaks using a sliding window. A peak exists when all surrounding pixels within some
	//radius are lower than the center pixel. A peak must also be greater than some minimum value
	//so peaks generated by noise are suppressed.
	//TODO: Handle the situation where two equal peaks are right next to each other.

	lines.clear();

	//Adjust radius based on how big the hough transform frame is. The 96.0f constant is arbitrary
	//but seems to perform well.
	const int radius = std::min(1l,std::lround(static_cast<float>(std::max(houghTransformFrame.width,houghTransformFrame.height)) / 96.0f));

	//Make the minimum peak value 3/4th of the highest peak value. This is somewhat arbitrary as
	//well but it out performs the statistical models I've tried.
	unsigned short maximumValue = 0;
	for(unsigned int x = 0;x < houghTransformFrame.width * houghTransformFrame.height;x++)
	{
		const unsigned int index = x * 3;
		const unsigned short value = *reinterpret_cast<const unsigned short*>(&houghTransformFrame.data[index]);
		maximumValue = std::max(maximumValue,value);
	}
	const unsigned short minimumValue = maximumValue * 3 / 4;
	if(minimumValue == 0)
		return; //No lines.

	auto ExtractValue = [&houghTransformFrame](const unsigned int x,const unsigned int y) -> unsigned short
	{
		if(x >= houghTransformFrame.width || y >= houghTransformFrame.height)
			return 0;

		const unsigned int index = (y * houghTransformFrame.width + x) * 3;
		return *reinterpret_cast<const unsigned short*>(&houghTransformFrame.data[index]);
	};

	for(unsigned int y = 0;y < houghTransformFrame.height;y++)
	{
		for(unsigned int x = 0;x < houghTransformFrame.width;x++)
		{
			const unsigned short value = ExtractValue(x,y);
			if(value < minimumValue)
				continue;

			bool peak = true;
			for(int ny = -radius;ny < radius + 1 && peak;ny++)
			{
				for(int nx = -radius;nx < radius + 1;nx++)
				{
					if(nx == 0 && ny == 0)
						continue;

					if(value < ExtractValue(static_cast<unsigned int>(static_cast<int>(x) + nx),
											static_cast<unsigned int>(static_cast<int>(y) + ny)))
					{
						peak = false;
						break;
					}
				}
			}

			if(peak)
			{
				//Convert peak into Hesse normal form theta and rho.
				//TODO: Maybe simplify how rho is stored...
				float theta = static_cast<float>(x) / static_cast<float>(houghTransformFrame.width) * M_PI;
				const float rMultiplier = static_cast<float>(houghTransformFrame.height) / 2.0f;
				const float maxR = hypotf(targetWidth,targetHeight);
				float rho = (static_cast<float>(y) - rMultiplier) * maxR / rMultiplier;

				//Make sure rho is always in a positive form to simplify working with the lines.
				if(rho < 0.0f)
				{
					theta = fmodf(theta + M_PI,2 * M_PI);
					rho *= -1.0f;
				}

				lines.push_back({theta,rho});
			}
		}
	}
}

static void ClusterizeLinesByTheta(const std::vector<Line>& lines,
								   std::vector<std::vector<Line>>& lineClusters)
{
	//Cluster lines with similar angles together. Generally, lines that are parallel should be
	//grouped together. The clustering is done in a single pass so the mean of a cluster moves after
	//each line is appended. This means similar lines could be put in different clusters because of
	//a different input order.

	lineClusters.clear();
	for(const Line& line : lines)
	{
		const float theta = line.theta;

		//Find an existing cluster for this line.
		bool foundCluster = false;
		for(std::vector<Line>& lineCluster : lineClusters)
		{
			const float meanTheta = MeanTheta(lineCluster);

			if(DifferenceTheta(theta,meanTheta) < DELTA_THETA)
			{
				lineCluster.push_back(line);
				foundCluster = true;
				break;
			}

			//Try to keep clusters that are PI apart together because they are parallel.
			const float alternativeTheta = fmod(theta + M_PI,2.0f * M_PI);
			if(DifferenceTheta(alternativeTheta,meanTheta) < DELTA_THETA)
			{
				lineCluster.push_back({alternativeTheta,line.rho * -1.0f});
				foundCluster = true;
				break;
			}
		}

		//Otherwise, start a new cluster.
		if(!foundCluster)
			lineClusters.push_back({line});
	}
}

static void FindPossiblePuzzleLines(std::vector<std::vector<Line>>& lineClusters,
									std::vector<std::vector<Line>>& possiblePuzzleLineClusters)
{
	//Search each cluster of lines for the widest four lines that are evenly spaced. Using a row
	//from a puzzle:
	//[#|#|#|#|#|#|#|#|#]
	//4     4     4     4
	//We're looking for the lines that line up with the the 4s. If we can find two sets of lines
	//that have the same count and are about pi/2 apart from each other, we've found a puzzle!
	//TODO: TONS of room for optimization here if necessary.

	//How close the gap between the lines should be from the expected average. Where the expected
	//average is (furthest_line - closest_line) / 3.
	//TODO: This should vary based on the input image size.
	constexpr float DELTA_THRESHOLD = 15; //Pixels.

	possiblePuzzleLineClusters.clear();

	//Sort cluster lines by rho so calculating the distance between each is straight forward.
	for(std::vector<Line>& lineCluster : lineClusters)
	{
		std::sort(lineCluster.begin(),lineCluster.end(),[](const Line& lhs,const Line& rhs) {
			return fabsf(lhs.rho) < fabsf(rhs.rho);
		});
	}

	auto GetSetBitIndexes = [](const unsigned int value,unsigned int* indexes)
	{
		unsigned int outputIndex = 0;
		for(unsigned int x = 0;x < 32 && outputIndex < 4;x++)
		{
			if((value & (1 << x)) > 0)
			{
				indexes[outputIndex] = x;
				outputIndex += 1;
			}
		}
	};
	for(const std::vector<Line>& lineCluster : lineClusters)
	{
		if(lineCluster.size() < 4)
			continue;
		else if(lineCluster.size() > 32)
			continue;

		float largestEvenlySpacedLinesRange = 0.0f;
		std::vector<Line> largestEvenlySpacedLines;
		const unsigned int finalSubset = 0b1111 << (lineCluster.size() - 4);
		for(unsigned int x = 0;x < finalSubset + 1;x++)
		{
#ifdef __linux
			const unsigned int count = __builtin_popcount(x);
#elif defined _WIN32
			const unsigned int count = __popcnt(x);
#endif
			if(count != 4)
				continue;

			unsigned int indexes[4];
			GetSetBitIndexes(x,indexes);

			const Line line0 = lineCluster[indexes[0]];
			const Line line1 = lineCluster[indexes[1]];
			const Line line2 = lineCluster[indexes[2]];
			const Line line3 = lineCluster[indexes[3]];

			const float range = line3.rho - line0.rho;
			if(range < largestEvenlySpacedLinesRange)
				continue;

			const float mean = range / 3.0f;
			const float delta0 = line1.rho - line0.rho;
			const float delta1 = line2.rho - line1.rho;
			const float delta2 = line3.rho - line2.rho;

			if(fabsf(delta0 - mean) < DELTA_THRESHOLD &&
			   fabsf(delta1 - mean) < DELTA_THRESHOLD &&
			   fabsf(delta2 - mean) < DELTA_THRESHOLD)
			{
				largestEvenlySpacedLinesRange = range;
				largestEvenlySpacedLines = {line0,line1,line2,line3};
			}
		}

		if(!largestEvenlySpacedLines.empty())
			possiblePuzzleLineClusters.push_back(largestEvenlySpacedLines);
	}
}

static void FindPuzzles(const std::vector<std::vector<Line>>& possiblePuzzleLineClusters,
						std::vector<std::pair<std::vector<Line>,std::vector<Line>>>& puzzleLines)
{
	//Search possible puzzle lines for two clusters that are rotated about pi/2 from each other.

	puzzleLines.clear();

	if(possiblePuzzleLineClusters.size() < 2)
		return;

	for(unsigned int y = 0;y < possiblePuzzleLineClusters.size();y++)
	{
		const std::vector<Line>& lines0 = possiblePuzzleLineClusters[y];
		const float theta0 = lines0[0].theta;
		for(unsigned int x = y + 1;x < possiblePuzzleLineClusters.size();x++)
		{
			const std::vector<Line>& lines1 = possiblePuzzleLineClusters[x];
			const float theta1 = lines1[0].theta;

			if(fabsf(static_cast<float>(M_PI_2) - DifferenceTheta(theta0,theta1)) < DELTA_THETA)
			{
				//Make the more horizontal lines come first so the puzzle doesn't get rotated when
				//it's extracted.
				if(cos(theta0) > cos(theta1))
					puzzleLines.push_back({lines0,lines1});
				else
					puzzleLines.push_back({lines1,lines0});
			}
		}
	}
}

bool PuzzleFinder::Find(const unsigned int targetWidth,const unsigned int targetHeight,const Image& houghTransformFrame,std::vector<Point>& puzzlePoints)
{
	//Find all of the lines in the hough transform.
	FindLines(targetWidth,targetHeight,houghTransformFrame,lines);

	//Group lines by angle.
	ClusterizeLinesByTheta(lines,lineClusters);

	//Search groups for the four largest evenly spaced lines.
	FindPossiblePuzzleLines(lineClusters,possiblePuzzleLineClusters);

	//Find sets of possible puzzle lines that are about PI/2 radians apart.
	FindPuzzles(possiblePuzzleLineClusters,puzzleLines);

	if(puzzleLines.empty())
		return false;

	//Only finding one puzzle at a time is supported.
	const auto puzzle = puzzleLines[0];

	//Find where the puzzle lines intersect to determine where the puzzle is. Only the outer
	//corners are needed.
	const auto& puzzleLines1 = puzzle.first;
	const auto& puzzleLines2 = puzzle.second;

	puzzlePoints.clear();
	for(unsigned int y = 0;y < 4;y += 3)
	{
		for(unsigned int x = 0;x < 4;x += 3)
		{
			float intersectionX = 0.0f;
			float intersectionY = 0.0f;
			IntersectLines(puzzleLines1[x],puzzleLines2[y],intersectionX,intersectionY);
			puzzlePoints.push_back({intersectionX,intersectionY});
		}
	}

	return true;
}
