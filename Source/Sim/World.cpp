#include "World.h"

#include <iostream>
#include <iomanip>
#include <functional>
#include <memory>

#include "../Util/Random.h"
#include "../Util/Common.h"
#include "../Util/Config.h"
#include "../ResourceManager/ResourceHolder.h"

#include "ColonyCreator.h"
#include "RandomColonyCreator.h"
#include "CustomColonyCreator.h"

constexpr int CHAR_SIZE = 14;

World::World(const Config& config)
:   m_map           (config)
,   m_people        (config.width, config.height)
,   m_colonyStatsManager    (config.colonies)
,   m_pConfig       (&config)
{
    createColonies();
    m_colonyStatsManager.initText(m_colonies);
}

const sf::Color& World::getColorAt(unsigned x, unsigned y) const
{
    return m_colonies[m_people(x, y).getColony()].colour;
}

void World::tryWrap(int& x, int& y) const
{
    if (x < 0)                              x = (m_pConfig->width - 1) + x;
    else if (x >= (int)m_pConfig->width)    x = x - m_pConfig->width;


    if (y < 0)                              y = (m_pConfig->height - 1) + y;
    else if (y >= (int)m_pConfig->height)   y = y - m_pConfig->height;
}

void World::drawText(sf::RenderWindow& window)
{
    m_colonyStatsManager.drawStats(window);
}

void World::draw(sf::RenderWindow& window)
{
    m_map.draw(window);
}

void World::createColonies()
{
    std::unique_ptr<ColonyCreator> colonyCreator;
    if (m_pConfig->customStart)
        colonyCreator = std::make_unique<CustomColonyCreator>(m_pConfig->imageName);
    else
        colonyCreator = std::make_unique<RandomColonyCreator>(m_pConfig->colonies);

    auto locations  = colonyCreator->createColonyLocations(*m_pConfig, m_map);
    m_colonies      = colonyCreator->createColonyStats();

    //Place colonies at the locations
    for (unsigned i = 1; i < m_colonies.size(); i++)
    {
        auto& location = locations[i];
        //place people at the location
        for (unsigned j = 0; j < m_colonies[i].startPeople; j++)
        {
            constexpr int radius = 5;
            int xOffset = Random::get().intInRange(-radius, radius);
            int yOffset = Random::get().intInRange(-radius, radius);

            int newLocationX = xOffset + location.x;
            int newLocationY = yOffset + location.y;

            if (newLocationX < 0 || newLocationX >= (int)m_pConfig->width)  continue;
            if (newLocationY < 0 || newLocationY >= (int)m_pConfig->height) continue;
            if (m_map.isWaterAt(newLocationX, newLocationY))                continue;

            ChildData data;
            data.strength   = Random::get().intInRange(m_colonies[i].strLow,
                                                       m_colonies[i].strHigh);
            data.isDiseased = false;
            data.colony     = i;

            m_people(newLocationX, newLocationY).init(data);

        }
    }
}


void World::update(sf::Image& image)
{
    Grid<Person> newPeople(m_pConfig->width, m_pConfig->height);
    m_colonyStatsManager.reset();

    randomCellForEach(*m_pConfig, [&](unsigned x, unsigned y)
    {
        auto& person = m_people(x, y);
        if (!person.isAlive())
            return;

        person.update();

        if (!person.isAlive()) return;

        unsigned colonyID  = person.getColony();
        unsigned strength  = person.getStrength();

        //Sometimes the loop will return early.
        //If it does, then it can call these functions
        auto endAlive = [&]()
        {
            newPeople(x, y) = person;
            m_colonyStatsManager.update(colonyID, strength);
            image.setPixel(x, y, getColorAt(x, y));
        };

        auto endDead = [&]()
        {
            image.setPixel(x, y, getColorAt(x, y));
        };

        //Get new location to move to
        auto nextMove = person.getNextMove();
        int xMoveTo = x + nextMove.x;
        int yMoveTo = y + nextMove.y;
        tryWrap(xMoveTo, yMoveTo);

        //Grid square to move to
        auto& movePerson = m_people(xMoveTo, yMoveTo);

        //If trying to move onto water or onto square where person of same colony is
        //, stay put
        if (m_map.isWaterAt(xMoveTo, yMoveTo))
        {
            endAlive();
            return;
        }
        else if (movePerson.getColony() == colonyID)
        {
            if (movePerson.isDiseased())
            {
                person.giveDisease();
            }

            endAlive();
            return;
        }

        //Try move to new spot
        //Fight other person if need be
        if (movePerson.getColony() != colonyID)
        {
            if (movePerson.isAlive())
            {
                person.fight(movePerson);
                if (!person.isAlive())
                {
                    endDead();
                    return;
                }
            }
        }
        //if the fight is survived, then good news!
        newPeople(xMoveTo, yMoveTo) = person;

        //try give birth
        if (person.getProduction() >= (unsigned)m_pConfig->reproductionThreshold)
        {
            //The person itself has moved to a new spot, so it is ok to mess with it's data now
            person.init(person.getChild());
        }
        else
        {
            //Kill the old person, the current person has now moved.
            //I know this is weird, but it works :^)
            person.kill();
        }

        endAlive();
    });
    m_people = std::move(newPeople);
}

