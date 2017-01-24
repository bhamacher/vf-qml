#include "veinqml.h"

#include "entitycomponentmap.h"

#include <ve_commandevent.h>
#include <ve_eventdata.h>

#include <vcmp_componentdata.h>
#include <vcmp_entitydata.h>
#include <vcmp_errordata.h>
#include <vcmp_introspectiondata.h>

Q_LOGGING_CATEGORY(VEIN_API_QML, "\e[1;37m<Vein.Api.QML>\033[0m")
Q_LOGGING_CATEGORY(VEIN_API_QML_VERBOSE, "\e[0;37m<Vein.Api.QML>\033[0m")

using namespace VeinEvent;
using namespace VeinComponent;

namespace VeinApiQml
{

  VeinQml::VeinQml(QObject *t_parent) : EventSystem(t_parent)
  {

  }

  VeinQml::~VeinQml()
  {

  }

  VeinQml::ConnectionState VeinQml::state() const
  {
    return m_state;
  }

  EntityComponentMap *VeinQml::getEntity(const QString &t_entityName) const
  {
    EntityComponentMap *retVal = 0;
    const int entityId = idFromEntityName(t_entityName); /// @todo this is a performance bottleneck

    if(entityId>=0 && m_entities.contains(entityId))
    {
      retVal = m_entities.value(entityId);
    }
    else
    {
      qCWarning(VEIN_API_QML) << "No entity found with name:" << t_entityName;
    }

    return retVal;
  }

  bool VeinQml::hasEntity(const QString &t_entityName) const
  {
    const int entityId = idFromEntityName(t_entityName);

    return entityId>=0 && m_entities.contains(entityId);
  }

  EntityComponentMap *VeinQml::getEntityById(int t_id) const
  {
    return m_entities.value(t_id, 0);
  }

  VeinQml *VeinQml::getStaticInstance()
  {
    return s_staticInstance;
  }

  void VeinQml::setStaticInstance(VeinQml *t_instance)
  {
    if(t_instance)
    {
      s_staticInstance = t_instance;
    }
  }

  void VeinQml::setRequiredIds(QList<int> t_requiredEntityIds)
  {
    vCDebug(VEIN_API_QML) << "Set required ids from:" << m_requiredIds << "to:" << t_requiredEntityIds;
    m_state = ConnectionState::VQ_IDLE;
    sigStateChanged(m_state);
    const QSet<int> toRemove = QSet<int>::fromList(m_requiredIds);
    const QSet<int> toAdd = QSet<int>::fromList(t_requiredEntityIds);

    foreach (int removedId, toRemove) {
      m_resolvedIds.removeAll(removedId);
      EntityComponentMap *toDelete = m_entities.value(removedId);
      m_entities.remove(removedId);
      toDelete->deleteLater();
      EntityData *eData = new EntityData();
      eData->setCommand(EntityData::Command::ECMD_UNSUBSCRIBE);
      eData->setEntityId(removedId);
      eData->setEventOrigin(EntityData::EventOrigin::EO_LOCAL);
      eData->setEventTarget(EntityData::EventTarget::ET_ALL);

      CommandEvent *cEvent = new CommandEvent(CommandEvent::EventSubtype::TRANSACTION, eData);

      vCDebug(VEIN_API_QML) << "Removing now obsolete subscription to entityId:" << removedId;

      emit sigSendEvent(cEvent);
    }

    foreach(int newId, toAdd) /// @bug currently it's possible to send subscription events for entities already subscribed to
    {
      EntityData *eData = new EntityData();
      eData->setCommand(EntityData::Command::ECMD_SUBSCRIBE);
      eData->setEntityId(newId);
      eData->setEventOrigin(EntityData::EventOrigin::EO_LOCAL);
      eData->setEventTarget(EntityData::EventTarget::ET_ALL);

      CommandEvent *cEvent = new CommandEvent(CommandEvent::EventSubtype::TRANSACTION, eData);

      emit sigSendEvent(cEvent);
    }
    m_requiredIds = t_requiredEntityIds;
  }

  bool VeinQml::processEvent(QEvent *t_event)
  {
    bool retVal = false;

    if(t_event->type()==CommandEvent::eventType())
    {
      CommandEvent *cEvent = 0;
      EventData *evData = 0;
      cEvent = static_cast<CommandEvent *>(t_event);
      Q_ASSERT(cEvent != 0);

      evData = cEvent->eventData();
      Q_ASSERT(evData != 0);

      if(cEvent->eventSubtype() == CommandEvent::EventSubtype::NOTIFICATION)
      {
        vCDebug(VEIN_API_QML_VERBOSE) << "Processing command event:" << cEvent << evData->type();

        /// @todo add support for network events (connected / disconnected / error)
        switch (evData->type())
        {
          case ComponentData::dataType():
          {
            ComponentData *cData=0;
            cData = static_cast<ComponentData *>(evData);
            Q_ASSERT(cData != nullptr);
            retVal = true;

            if(m_entities.contains(cData->entityId())) /// @note component data is only processed after the introspection has been processed
            {
              m_entities.value(cData->entityId())->processComponentData(cData);
            }
            break;
          }
          case EntityData::dataType():
          {
            EntityData *eData=0;
            eData = static_cast<EntityData *>(evData);
            retVal = true;
            int entityId =eData->entityId();

            switch(eData->eventCommand())
            {
              case VeinComponent::EntityData::ECMD_REMOVE:
              {
                if(m_entities.contains(entityId))
                {
                  EntityComponentMap *eMap = m_entities.value(entityId);
                  eMap->setState(EntityComponentMap::DataState::ECM_REMOVED);

                  m_entities.remove(entityId);
                  delete eMap;

                  if(m_requiredIds.contains(entityId))
                  {
                    m_resolvedIds.removeAll(entityId);
                    qCCritical(VEIN_API_QML_INTROSPECTION) << "Required entity was removed remotely, entity id:" << entityId;
                    m_state = ConnectionState::VQ_ERROR;
                    emit sigStateChanged(m_state);
                  }
                }
                break;
              }
              default:
                break;
            }
            break;
          }
          case ErrorData::dataType(): /// @todo add message queue and check if the error belongs to actions taken from this client
          {
            ErrorData *errData=0;
            errData = static_cast<ErrorData *>(evData);
            qCWarning(VEIN_API_QML_INTROSPECTION) << "Received error:" <<errData->errorDescription();
            break;
          }
          case IntrospectionData::dataType():
          {
            IntrospectionData *iData=0;
            iData = static_cast<IntrospectionData *>(evData);
            retVal = true;
            int entityId = iData->entityId();
            vCDebug(VEIN_API_QML) << "Received introspection data for entity:" << entityId;

            if(m_entities.contains(entityId) == false)
            {
              vCDebug(VEIN_API_QML) << "added introspection for entity:" << entityId;
              EntityComponentMap *eMap = new EntityComponentMap(entityId, iData->jsonData(), this);
              m_entities.insert(entityId, eMap);
              connect(eMap, &EntityComponentMap::sigSendEvent, this, &VeinQml::sigSendEvent);
              connect(eMap, &EntityComponentMap::sigLoadedChanged, this, &VeinQml::onEntityLoaded);
              eMap->setState(EntityComponentMap::DataState::ECM_PENDING);
            }
            break;
          }
          default:
            break;
        }
      }
    }
    return retVal;
  }

  void VeinQml::onEntityLoaded(int t_entityId)
  {
    m_state = ConnectionState::VQ_IDLE;
    emit sigStateChanged(m_state);
    if(m_requiredIds.contains(t_entityId))
    {
      vCDebug(VEIN_API_QML) << "Fetched required entity:" << t_entityId;

      /// @todo PRIO check ecm_ready use
      //m_entities.value(t_entityId)->setState(EntityComponentMap::DataState::ECM_READY);
      m_resolvedIds.append(t_entityId);
      emit sigEntityAvailable(nameFromEntityId(t_entityId)); // needs to be called before sigStateChanged(), or the list of entities may be already deleted from a setRequiredIds() call
      if(m_state != ConnectionState::VQ_LOADED)
      {
        qSort(m_requiredIds);
        qSort(m_resolvedIds);
        if(m_requiredIds == m_resolvedIds)
        {
          vCDebug(VEIN_API_QML) << "All required entities resolved";
          m_state = ConnectionState::VQ_LOADED;
          emit sigStateChanged(m_state);
        }
      }
    }
  }

  int VeinQml::idFromEntityName(const QString &t_entityName) const
  {
    int retVal = -1;
    if(t_entityName.isEmpty() == false)
    {
      foreach(int tmpKey, m_entities.keys())
      {
        EntityComponentMap *eMap = m_entities.value(tmpKey);
        if(eMap->value("EntityName") == t_entityName) /// @todo replace with cross reference list
        {
          retVal = tmpKey;
          break;
        }
      }
    }
    return retVal;
  }

  QString VeinQml::nameFromEntityId(int t_entityId) const
  {
    QString retVal;
    if(m_entities.contains(t_entityId))
    {
      retVal = m_entities.value(t_entityId)->value("EntityName").value<QString>(); /// @todo replace with cross reference list
    }
    return retVal;
  }


  VeinQml *VeinQml::s_staticInstance = 0;
}
