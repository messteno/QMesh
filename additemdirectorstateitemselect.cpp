#include "additemdirector.h"
#include "additemdirectorstateitemselect.h"
#include "additemdirectorstateitemadd.h"

AddItemDirectorStateItemSelect::AddItemDirectorStateItemSelect()
{
}

AddItemDirectorState* AddItemDirectorStateItemSelect::getInstance()
{
    static AddItemDirectorStateItemSelect self;
    return &self;
}

void AddItemDirectorStateItemSelect::widgetButtonPushed(AddItemDirector *director, AddItemWidget *widget)
{
    director->processWidgetSelected(widget);
    changeState(director, AddItemDirectorStateItemAdd::getInstance());
}
