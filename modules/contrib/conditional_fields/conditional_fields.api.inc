<?php

/**
 * @file
 * Contains conditional fields logic of modify forms.
 */

use Drupal\Core\Form\FormStateInterface;
use Drupal\Component\Utility\NestedArray;
use Drupal\Core\Field\WidgetBase;
use Drupal\field\Entity\FieldStorageConfig;
use Drupal\Core\Entity\Display\EntityFormDisplayInterface;
use Drupal\Component\Render\MarkupInterface;

/**
 * Loads all dependencies from the database.
 *
 * The result can be filtered by providing an entity type and a bundle name.
 */
function conditional_fields_load_dependencies_entity($entity_type = NULL, $bundle = NULL) {
  $dependencies = \Drupal::entityTypeManager()
    ->getStorage('conditional_field')
    ->loadByProperties([
      'entity_type' => $entity_type,
      'bundle' => $bundle,
    ]);
  return $dependencies;
}

/**
 * An after_build callback for forms with dependencies.
 *
 * Builds and attaches #states properties to dependent fields, adds additional
 * visual effects handling to the States API and attaches a validation callback
 * to the form that handles validation of dependent fields.
 */
function conditional_fields_form_after_build($form, FormStateInterface &$form_state) {
  // Dependencies data is attached in conditional_fields_element_after_build().
  if (empty($form['#conditional_fields'])) {
    return $form;
  }

  $effects = [];

  $form_obj = $form_state->getFormObject();
  if (method_exists($form_obj, 'getFormDisplay')) {
    /** @var EntityFormDisplayInterface $form_display */
    $form_display = $form_obj->getFormDisplay($form_state);
  }
  else {
    return $form;
  }

  // Cycle all dependents.
  foreach ($form['#conditional_fields'] as $dependent => $dependent_info) {
    $states = [];

    if (empty($dependent_info['dependees'])) {
      continue;
    }

    $dependent_location = array_merge([], [$dependent]);
    $dependent_form_field = NestedArray::getValue($form, $dependent_location);

    // Cycle the dependant's dependees.
    foreach ($dependent_info['dependees'] as $dependency) {
      $dependee = $dependency['dependee'];

      if (empty($form['#conditional_fields'][$dependee])) {
        continue;
      }

      $dependee_info = $form['#conditional_fields'][$dependee];

      end($dependee_info['parents']);
      $unset = key($dependee_info['parents']);
      if (is_int($dependee_info['parents'][$unset]) && $dependee_info['parents'][$unset] > 0) {
        unset($dependee_info['parents'][$unset]);
      }

      $dependee_form_field = NestedArray::getValue($form, $dependee_info['parents']);
      $options = $dependency['options'];

      if (!empty($options['values']) && is_string($options['values'])) {
        $options['values'] = explode("\r\n", $options['values']);
      }

      $options['selector'] = conditional_fields_get_selector($options['selector'], $dependee_form_field);

      $state = conditional_fields_get_state($form_display, $dependee, $dependee_form_field, $options, $form_state);

      // Add validation callback to element if the dependency can be evaluated.
      if (in_array($options['condition'], [
        'value',
        'empty',
        '!empty',
        'checked',
        '!checked',
      ])) {
        _conditional_fields_element_add_property($dependent_form_field, '#element_validate', 'conditional_fields_dependent_validate', 'append');
      }

      conditional_fields_add_state_to_group($states, $state, $options);

      $selector = conditional_fields_field_selector(NestedArray::getValue($form, [$dependent_location[0]]));
      $effects[$selector] = conditional_fields_get_effect($options);

    }

    if (empty($states)) {
      continue;
    }

    // Save the modified field back into the form.
    NestedArray::setValue($form, $dependent_location, $dependent_form_field);

    // Map the states based on the conjunctions.
    $states_new = [];
    foreach ($states as $state_key => $value) {
      // As the main object is ANDed together we can add the AND items directly.
      if (!empty($states[$state_key]['AND'])) {
        $states_new[$state_key] = $states[$state_key]['AND'];
      }
      // The OR and XOR groups are moved into a sub-array that has numeric keys
      // so that we get a JSON array and not an object, as required by the States
      // API for OR and XOR groupings.
      if (!empty($states[$state_key]['OR'])) {
        $or = [];
        foreach ($states[$state_key]['OR'] as $constraint_key => $constraint_value) {
          $or[] = [$constraint_key => $constraint_value];
        }
        // '1' as a string so that we get an object (which means logic groups
        // are ANDed together).
        $states_new[$state_key]['1'] = $or;
      }
      if (!empty($states[$state_key]['XOR'])) {
        $xor = ['xor'];
        foreach ($states[$state_key]['XOR'] as $constraint_key => $constraint_value) {
          $xor[] = [$constraint_key => $constraint_value];
        }
        // '2' as a string so that we get an object.
        $states_new[$state_key]['2'] = $xor;
      }
    }
    $states = $states_new;

    // Add the #states property to the dependent field.
    NestedArray::setValue($form, array_merge($dependent_location, ['#states']), $states);
  }

  $form['#attached']['library'][] = 'conditional_fields/conditional_fields';
  // Add effect settings to the form.
  if ($effects) {
    $form['#attached']['drupalSettings']['conditionalFields'] = [
      'effects' => $effects,
    ];
  }

  // Validation callback to manage dependent fields validation.
  $form['#validate'][] = 'conditional_fields_form_validate';
  // Initialize validation information every time the form is rendered to avoid
  // stale data after a failed submission.
  $form_state->setValue('conditional_fields_untriggered_dependents', []);

  return $form;
}

/**
 * Build a jQuery selector if it was not overridden by a custom value.
 *
 * Note that this may be overridden later by a state handler.
 */
function conditional_fields_get_selector($options_selector, $dependee_form_field) {
  if (!$options_selector) {
    $selector = conditional_fields_field_selector($dependee_form_field);
  }
  else {
    // Replace the language placeholder in the selector with current language.
    $selector = str_replace('%lang', $dependee_form_field['#language'], $options_selector);
  }
  return $selector;
}

/**
 * Get list of states for the pair from the options.
 *
 * @see hook_get_state
 *
 * @param EntityFormDisplayInterface $form_display
 *   Form display.
 * @param string $dependee
 *   Machine name of control field.
 * @param array $dependee_form_field
 *   Nested array of control field.
 * @param array $options
 *   Settings of dependency.
 * @param FormStateInterface $form_state
 *   Form state interface
 *
 * @return array
 *   List of states.
 */
function conditional_fields_get_state(EntityFormDisplayInterface $form_display, $dependee, $dependee_form_field, $options, $form_state = null) {
  $state = [];
  /** @var \Drupal\conditional_fields\ConditionalFieldsHandlersManager $type */
  $type = \Drupal::service('plugin.manager.conditional_fields_handlers');

  if ($options['condition'] != 'value') {
    // Conditions different than "value" are always evaluated against TRUE.
    $state = [$options['state'] => [$options['selector'] => [$options['condition'] => TRUE]]];
  }
  else {
    $field_name = explode('[', $dependee_form_field['#name']);
    $dependee_form_state = isset($dependee_form_field['#field_parents'], $field_name[0], $form_state) ? WidgetBase::getWidgetState($dependee_form_field['#field_parents'], $field_name[0], $form_state) : NULL;
    $dependee_form_field['#original_name'] = $field_name[0];
    $dependee_display = $form_display->getComponent($dependee);
    if (is_array($dependee_display) && array_key_exists('type', $dependee_display)) {
      $widget_id = $dependee_display['type'];
    }

    // @todo: Use field cardinality instead of number of values that was
    // selected on manage dependency tab. As temporary solution put cardinality
    // in $options. Format of #states depend on field widget and field
    // cardinality (it can be like value: string and value: [array]).
    if ($field_config = FieldStorageConfig::loadByName($form_display->getTargetEntityTypeId(), $dependee)) {
      $options['field_cardinality'] = $field_config->getCardinality();
    }

    // Execute special handler for fields that need further processing.
    // The handler has no return value. Modify the $state parameter by
    // reference if needed.
    if (isset($widget_id)) {
      $handler_id = 'states_handler_' . $widget_id;
      /** @var Drupal\conditional_fields\ConditionalFieldsHandlersPluginInterface $handler */
      $handler = $type->createInstance($handler_id);
      $state = $handler->statesHandler($dependee_form_field, $dependee_form_state, $options);
    }

    if (empty($state)) {
      // If states empty Default plugin.
      /** @var Drupal\conditional_fields\ConditionalFieldsHandlersPluginInterface $default_handler */
      $default_handler = $type->createInstance('states_handler_default_state');
      $state = $default_handler->statesHandler($dependee_form_field, $dependee_form_state, $options);
    }
  }

  return $state;
}

/**
 * Merge field states to general list.
 *
 * @param array $states
 *   List of all form states.
 * @param array $state
 *   List of field states.
 * @param array $options
 *   Field CF settings.
 */
function conditional_fields_add_state_to_group(&$states, $state, $options) {
  // Add the $state into the correct logic group in $states.
  foreach ($state as $key => $constraints) {
    if (empty($states[$key][$options['grouping']])) {
      $states[$key][$options['grouping']] = $constraints;
    }
    else {
      $states[$key][$options['grouping']] = array_merge($states[$key][$options['grouping']], $constraints);
    }
  }
}

/**
 * Returns js effect for field.
 *
 * @param array $options
 *   Field CF settings.
 *
 * @return array
 *   Effect with options.
 */
function conditional_fields_get_effect($options) {
  // Build effect settings for effects with options.
  // TODO: add dependee key to allow different effects on the same selector.
  if ($options['effect'] && $options['effect'] != 'show') {
    // Convert numeric strings to numbers.
    foreach ($options['effect_options'] as &$effect_option) {
      if (is_numeric($effect_option)) {
        $effect_option += 0;
      }
    }
    return [
      'effect' => $options['effect'],
      'options' => $options['effect_options'],
    ];
  }
  return [];
}

/**
 * Dependent field validation callback.
 *
 * If the dependencies of a dependent field are not triggered, the validation
 * errors that it might have thrown must be removed, together with its submitted
 * values. This will simulate the field not being present in the form at all.
 * In this field-level callback we just collect needed information and store it
 * in $form_state. Values and errors will be removed in a single sweep in
 * conditional_fields_form_validate(), which runs at the end of the validation
 * cycle.
 *
 * @see conditional_fields_form_validate()
 */
function conditional_fields_dependent_validate($element, FormStateInterface &$form_state, $form) {
  if (!isset($form['#conditional_fields'])) {
    return;
  }

  $dependent = $form['#conditional_fields'][reset($element['#array_parents'])];

  // Check if this field's dependencies were triggered.
  $triggered = conditional_fields_evaluate_dependencies($dependent, $form, $form_state);
  $return = FALSE;

  // TODO: refactor this!
  if ($evaluated_dependencies = conditional_fields_evaluate_dependencies($dependent, $form, $form_state, FALSE)) {
    foreach ($evaluated_dependencies[reset($dependent['field_parents'])] as $operator) {
      foreach ($operator as $state => $result) {
        if (($result && $state == 'visible' && $triggered) || (!$result && $state == '!visible' && !$triggered)) {
          $return = TRUE;
        }
        if (($result && $state == 'required' && $triggered) || (!$result && $state == '!required' && !$triggered)) {
          $return = TRUE;
          $key_exists = NULL;
          $values = $form_state->getValues();
          $input_state = NestedArray::getValue($values, $dependent['field_parents'], $key_exists);
          if ($key_exists && isset($input_state['add_more'])) {
            // Remove the 'value' of the 'add more' button.
            unset($input_state['add_more']);
          }
          $input_state = (is_null($input_state)) ? [] : $input_state;
          if (isset($dependent['field_parents'][0])) {
            $field = FieldStorageConfig::loadByName($form['#entity_type'], $dependent['field_parents'][0]);
          } else {
            $field = null;
          }
          if (empty($input_state)) {
            if (isset($element['widget']['#title'])) {
              $title = $element['widget']['#title'];
            }
            elseif (isset($dependent['field_parents'][0])) {
              $title = $dependent['field_parents'][0];
            } elseif ( $field ) {
              $title = $field->getName();
            }

            $form_state->setError($element, t('%name is required.', ['%name' => $title]));
          }
        }
      }
    }
  }

  if ($return) {
    return;
  }

  // Mark submitted values for removal. We have to remove them after all fields
  // have been validated to avoid collision between dependencies.
  $form_state_addition['parents'] = $dependent['field_parents'];


  // Optional behavior: reset the field to its default values.
  // Default values are always valid, so it's safe to skip validation.
  if (!empty($element['#conditional_fields_reset_if_untriggered']) && !$triggered) {
    $form_state_addition['reset'] = TRUE;
  }

  // Tag validation errors previously set on this field for removal in
  // conditional_fields_form_validate().
  $errors = $form_state->getErrors();

  if ($errors) {
    $error_key = reset($dependent['field_parents']);
    foreach ($errors as $name => $error) {
      // An error triggered by this field might have been set on a descendant
      // element. This also means that so there can be multiple errors on the
      // same field (even though Drupal doesn't support multiple errors on the
      // same element).
      if (strpos((string)$name, $error_key) === 0) {
        $field_errors[$name] = $error;
      }
    }
  }

  if (!empty($field_errors)) {
    $form_state_addition['errors'] = $field_errors;
  }

  $fiel_state_values_count = count($form_state->getValue('conditional_fields_untriggered_dependents'));
  $form_state->setValue([
    'conditional_fields_untriggered_dependents',
    $fiel_state_values_count,
  ], $form_state_addition);
}

/**
 * Evaluate a set of dependencies for a dependent field.
 *
 * @param array $dependent
 *   The field form element in the current language.
 *
 * @return array|bool $evaluated_dependees.
 *   Evaluated dependencies array.
 */
function conditional_fields_evaluate_dependencies($dependent, $form, $form_state, $grouping = TRUE) {
  $dependencies = $form['#conditional_fields'][reset($dependent['field_parents'])]['dependees'];
  $evaluated_dependees = [];

  foreach ($dependencies as $dependency_id => $dependency) {
    // Extract field values from submitted values.
    $dependee = $dependency['dependee'];

    // Skip any misconfigured conditions.
    if (empty($form['#conditional_fields'][$dependee]['parents'])) {
      continue;
    }

    $dependee_parents = $form['#conditional_fields'][$dependee]['parents'];

    // We have the parents of the field, but depending on the entity type and
    // the widget type, they may include additional elements that are actually
    // part of the value. So we find the depth of the field inside the form
    // structure and use the parents only up to that depth.
    $dependee_parents_keys = array_flip($dependee_parents);
    $dependee_parent = NestedArray::getValue($form, array_slice($dependee_parents, 0, $dependee_parents_keys[$dependee]));
    $values = conditional_fields_form_field_get_values($dependee_parent[$dependee], $form_state);

    // Remove the language key.
    if (isset($dependee_parent[$dependee]['#language'], $values[$dependee_parent[$dependee]['#language']])) {
      $values = $values[$dependee_parent[$dependee]['#language']];
    }

    if ($grouping) {
      $evaluated_dependees[reset($dependent['field_parents'])][$dependency['options']['grouping']][] = conditional_fields_evaluate_dependency('edit', $values, $dependency['options']);
    }
    else {
      $evaluated_dependees[reset($dependent['field_parents'])][$dependency['options']['grouping']][$dependency['options']['state']] = conditional_fields_evaluate_dependency('edit', $values, $dependency['options']);
    }
  }

  if ($grouping) {
    return conditional_fields_evaluate_grouping($evaluated_dependees[reset($dependent['field_parents'])]);
  }

  return $evaluated_dependees;
}

/**
 * Evaluates an array with 'AND', 'OR' and 'XOR' groupings.
 *
 * Each containing a list of boolean values.
 */
function conditional_fields_evaluate_grouping($groups) {
  $or = $and = $xor = TRUE;
  if (!empty($groups['OR'])) {
    $or = in_array(TRUE, $groups['OR']);
  }
  if (!empty($groups['AND'])) {
    $and = !in_array(FALSE, $groups['AND']);
  }
  if (!empty($groups['XOR'])) {
    $xor = array_sum($groups['XOR']) == 1;
  }
  return $or && $and && $xor;
}

/**
 * Validation callback for any form with conditional fields.
 *
 * This validation callback is added to all forms that contain fields with
 * dependencies. It removes all validation errors from dependent fields whose
 * dependencies are not triggered, which were collected at field-level
 * validation in conditional_fields_dependent_validate().
 *
 * @see conditional_fields_dependent_validate()
 */
function conditional_fields_form_validate($form, FormStateInterface &$form_state) {
  if (empty($form_state->getValue('conditional_fields_untriggered_dependents'))) {
    return;
  }

  $untriggered_dependents_errors = [];

  foreach ($form_state->getValue('conditional_fields_untriggered_dependents') as $field) {
    $parent = [$field['parents'][0]];
    $dependent = NestedArray::getValue($form, $parent);
    $field_values_location = conditional_fields_form_field_get_values($dependent, $form_state);

    // If we couldn't find a location for the field's submitted values, let the
    // validation errors pass through to avoid security holes.
    if (!isset($field_values_location[reset($dependent['#array_parents'])])) {
      if (!empty($field['errors'])) {
        $untriggered_dependents_errors = array_merge($untriggered_dependents_errors, $field['errors']);
      }
      continue;
    }

    // Save the changed array back in place.
    // Do not use form_set_value() since it assumes
    // that the values are located at
    // $form_state['values'][ ... $element['#parents'] ... ], while the
    // documentation of hook_field_widget_form() states that field values are
    // $form_state['values'][ ... $element['#field_parents'] ... ].
    $values = $form_state->getValues();
    NestedArray::setValue($values, $dependent['#field_parents'], $field_values_location);

    if (!empty($field['errors'])) {
      $untriggered_dependents_errors = array_merge($untriggered_dependents_errors, $field['errors']);
    }
  }

  if (!empty($untriggered_dependents_errors)) {
    // Since Drupal provides no clean way to selectively remove error messages,
    // we have to store all current form errors and error messages, clear them,
    // filter out from our stored values the errors originating from untriggered
    // dependent fields, and then reinstate remaining errors and messages.
    $errors = array_diff_assoc((array) $form_state->getErrors(), $untriggered_dependents_errors);
    $form_state->clearErrors();
    $error_messages = Drupal::messenger()->messagesByType( 'error' );

    $removed_messages = array_values($untriggered_dependents_errors);

    // Reinstate remaining errors.
    foreach ($errors as $name => $error) {
      $form_state->setErrorByName($name, $error);
      // form_set_error() calls drupal_set_message(), so we have to filter out
      // these from the messages to avoid duplicates.
      $removed_messages[] = $error;
    }

    // Reinstate remaining error messages (which, at this point,
    // are messages that were originated outside of the validation process).
    if (!empty($error_messages['error'])) {
      $error_messages_array = $error_messages['error'] instanceof MarkupInterface ? $error_messages['error']->jsonSerialize() : $error_messages['error'];
      foreach (array_diff($error_messages_array, $removed_messages) as $message) {
        Drupal::messenger()->addMessage( $message, 'error' );
      }
    }
  }
}

/**
 * Extracts submitted field values during form validation.
 *
 * @return array|NULL
 *   The requested field values parent. Actual field vales are stored under the
 *   key $element['#field_name'].
 */
function conditional_fields_form_field_get_values($element, FormStateInterface $form_state) {
  // Fall back to #parents to support custom dependencies.
  $parents = !empty($element['#array_parents']) ? $element['#array_parents'] : $element['#parents'];
  $values = $form_state->getValues();
  return NestedArray::getValue($values, $parents);
}

/**
 * Evaluate if a dependency meets the requirements to be triggered.
 *
 * @param string $context
 *   Options:
 *   'edit' if $values are extracted from $form_state.
 *   'view' if $values are extracted from an entity.
 *
 * @return bool
 *   Can the dependency be triggered?
 */
function conditional_fields_evaluate_dependency($context, $values, $options) {
  if ($options['values_set'] == CONDITIONAL_FIELDS_DEPENDENCY_VALUES_WIDGET) {
    $dependency_values = $context == 'view' ? $options['value'] : $options['value_form'];

    if ($options['condition'] === '!empty') {
      $values = (isset($values[0]['value'])) ? $values[0]['value'] : $values;
      $values = ($values === '_none') ? '' : $values;
      return (!empty($values)) ? TRUE : FALSE;
    }

    if ($options['condition'] === 'empty') {
      $values = (isset($values[0]['value'])) ? $values[0]['value'] : $values;
      $values = ($values === '_none') ? '' : $values;
      return (empty($values)) ? TRUE : FALSE;
    }

    // The BooleanList widget provides an empty array as $dependency_values,
    // thus checking this field requires a different handling in case of
    // 'checked or '!checked' conditions, where $value has 0 or 1.
    if ($options['condition'] === 'checked' || $options['condition'] === '!checked') {
      $dependency_values = (int) ($options['condition'] === 'checked');
    }


    // Simple case: both values are strings or integers. Should never happen in
    // view context, but does no harm to check anyway.
    if (!is_array($values) || (is_array($values) && empty($values))) {
      // Options elements consider "_none" value same as empty.
      $values = $values === '_none' ? '' : $values;

      if (!is_array($dependency_values)) {
        // Some widgets store integers, but values saved in $dependency_values
        // are always strings. Convert them to integers because we want to do a
        // strict equality check to differentiate empty strings from '0'.
        if (is_int($values) && is_numeric($dependency_values)) {
          $dependency_values = (int) $dependency_values;
        }

        return $dependency_values === $values;
      }

      // If $values is a string and $dependency_values an array, convert $values
      // to the standard field array form format. This covers cases like single
      // value textfields.
      $values = [['value' => $values]];
    }

    // If we are in form context, we are almost done.
    if ($context == 'edit') {
      // If $dependency_values is not an array, we can only assume that it
      // should map to the first key of the first value of $values.
      if (!is_array($dependency_values)) {
        if (is_null(current($values)) || empty($options['value'][0])) {
          return FALSE;
        }
        $key = current(array_keys((array) current($values)));
        $dependency_values = [[$key => $options['value'][0][$key]]];
        $temp[][$key] = $values[0][$key];
        $values = $temp;
      }

      // Compare arrays recursively ignoring keys, since multiple select widgets
      // values have numeric keys in form format and string keys in storage
      // format.
      return array_values($dependency_values) == array_values($values);
    }

    // $values, when viewing fields, may contain all sort of additional
    // information, so filter out from $values the keys that are not present in
    // $dependency_values.
    // Values here are alway keyed by delta (regardless of multiple value
    // settings).
    foreach ($values as $delta => &$value) {
      if (isset($dependency_values[$delta])) {
        $value = array_intersect_key($value, $dependency_values[$delta]);

        foreach ($value as $key => &$element_value) {
          if (isset($dependency_values[$delta][$key]) && is_int($dependency_values[$delta][$key]) && is_numeric($element_value)) {
            $element_value = (int) $element_value;
          }
        }
      }
    }

    // Compare values.
    foreach ($dependency_values as $delta => $dependency_value) {
      if (!isset($values[$delta])) {
        return FALSE;
      }
      foreach ($dependency_value as $key => $dependency_element_value) {
        // Ignore keys set in the field and not in the dependency.
        if (isset($values[$delta][$key]) && $values[$delta][$key] !== $dependency_element_value) {
          return FALSE;
        }
      }
    }

    return TRUE;
  }

  // Flatten array of values.
  $reference_values = [];
  foreach ((array) $values as $value) {
    // TODO: support multiple values.
    $reference_values[] = is_array($value) ? array_shift($value) : $value;
  }

  // Regular expression method.
  if ($options['values_set'] == CONDITIONAL_FIELDS_DEPENDENCY_VALUES_REGEX) {
    foreach ($reference_values as $reference_value) {
      if (!preg_match('/' . $options['regex'] . '/', $reference_value)) {
        return FALSE;
      }
    }
    return TRUE;
  }

  if (!empty($options['values']) && is_string($options['values'])) {
    $options['values'] = explode("\r\n", $options['values']);
  }

  switch ($options['values_set']) {
    case CONDITIONAL_FIELDS_DEPENDENCY_VALUES_AND:
      $diff = array_diff($options['values'], $reference_values);
      return empty($diff);

    case CONDITIONAL_FIELDS_DEPENDENCY_VALUES_OR:
      $intersect = array_intersect($options['values'], $reference_values);
      return !empty($intersect);

    case CONDITIONAL_FIELDS_DEPENDENCY_VALUES_XOR:
      $intersect = array_intersect($options['values'], $reference_values);
      return count($intersect) == 1;

    case CONDITIONAL_FIELDS_DEPENDENCY_VALUES_NOT:
      $intersect = array_intersect($options['values'], $reference_values);
      return empty($intersect);
  }

  return TRUE;
}
