function nav_init (time_begin, time_end)
{
  var all_images;
  var i;

  all_images = document.getElementsByTagName ("img");
  for (i = 0; i < all_images.length; i++)
  {
    if (all_images[i].className != "graph_image")
      continue;

    all_images[i].navTimeBegin = new Number (time_begin);
    all_images[i].navTimeEnd   = new Number (time_end);

    all_images[i].navBaseURL = all_images[i].src.replace (/;(begin|end)=[^;]*/g, '');
  }

  return (true);
} /* nav_init */

function nav_time_reset (img_id ,diff)
{
  var img;

  img = document.getElementById (img_id);
  if (!img)
    return (false);

  img.navTimeEnd = new Number ((new Date ()).getTime () / 1000);
  img.navTimeBegin = new Number (img.navTimeEnd - diff);

  img.src = img.navBaseURL + ";"
    + "begin=" + img.navTimeBegin.toFixed (0) + ";"
    + "end=" + img.navTimeEnd.toFixed (0);

  return (true);
}

function nav_time_change_obj (img, factor_begin, factor_end)
{
  var diff;

  if (!img)
    return (false);

  if (!img.navTimeEnd || !img.navTimeBegin)
    return (false);

  diff = img.navTimeEnd - img.navTimeBegin;
  if (diff <= 300)
    return (true);

  img.navTimeBegin += (diff * factor_begin);
  img.navTimeEnd   += (diff * factor_end);

  img.src = img.navBaseURL + ";"
    + "begin=" + img.navTimeBegin.toFixed (0) + ";"
    + "end=" + img.navTimeEnd.toFixed (0);

  return (true);
} /* nav_time_change */

function nav_time_change (img_id, factor_begin, factor_end)
{
  var diff;

  if (img_id == '*')
  {
    var all_images;

    all_images = document.getElementsByTagName ("img");
    for (i = 0; i < all_images.length; i++)
    {
      if (all_images[i].className != "graph_image")
        continue;
    
      nav_time_change_obj (all_images[i], factor_begin, factor_end);
    }
  }
  else
  {
    var img;

    img = document.getElementById (img_id);
    if (!img)
      return (false);

    nav_time_change_obj (img, factor_begin, factor_end);
  }

  return (true);
} /* nav_time_change */

function nav_move_earlier (img_id)
{
  return (nav_time_change (img_id, -0.2, -0.2));
} /* nav_move_earlier */

function nav_move_later (img_id)
{
  return (nav_time_change (img_id, +0.2, +0.2));
} /* nav_move_later */

function nav_zoom_in (img_id)
{
  return (nav_time_change (img_id, +0.2, -0.2));
} /* nav_zoom_in */

function nav_zoom_out (img_id)
{
  return (nav_time_change (img_id, (-1.0 / 3.0), (1.0 / 3.0)));
}
/* vim: set sw=2 sts=2 et : */
