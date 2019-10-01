<?php

namespace Drupal\entity_overlay\Annotation;

use Drupal\Component\Annotation\Plugin;

/**
 * Defines the entity trait plugin annotation object.
 *
 * Plugin namespace: Plugin\EntityOverlay.
 *
 * @see plugin_api
 *
 * @Annotation
 */
class EntityOverlayDialog extends Plugin {

  /**
   * The plugin ID.
   *
   * @var string
   */
  public $id;

  /**
   * The plugin label.
   *
   * @var \Drupal\Core\Annotation\Translation
   *
   * @ingroup plugin_translatable
   */
  public $label;

  /**
   * The library to attach for this dialog.
   *
   * @var string
   */
  public $library;

}
